#include <stdio.h>
#include <string.h>
#include <raylib.h>
#include "net_client.h"
#include "net.h"
#include "../player/player.h"
#include "../world/worldobject.h"
#include "../game/game_state.h"
#include "../game/game_logic.h"

static ENetHost *sClient    = NULL;
static ENetPeer *sServer    = NULL;
static bool      sConnected = false;

// ─────────────────────────────────────────────────────────────
// Remote player interpolation state
// ─────────────────────────────────────────────────────────────

// How far behind the latest snapshot we render remote players.
// Must be >= one snapshot interval (1/SNAPSHOT_HZ) so there are
// always two snapshots to lerp between. 50ms is comfortable at 60Hz.
#define INTERP_DELAY 0.05

#define INTERP_BUF 16  // ring buffer — must be power of two

typedef struct {
    Vector3 pos;
    float   yaw;
    double  timestamp;
} RemoteSnapshot;

typedef struct {
    RemoteSnapshot buf[INTERP_BUF];
    int            head;   // index of newest entry
    int            count;  // how many entries are valid
} RemoteInterp;

static RemoteInterp sInterp[MAX_PLAYERS];

// Push a new snapshot sample into the ring buffer for slot s.
static void InterpPush(int s, Vector3 pos, float yaw, double t) {
    RemoteInterp *ri = &sInterp[s];
    ri->head = (ri->head + 1) & (INTERP_BUF - 1);
    ri->buf[ri->head] = (RemoteSnapshot){ pos, yaw, t };
    if (ri->count < INTERP_BUF) ri->count++;
}

// Call each frame to advance interpolated positions into gameState.
// Must be called before rendering.
void ClientInterpolateRemote(void) {
    double renderTime = GetTime() - INTERP_DELAY;

    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (s == localSlot) continue;
        RemoteInterp *ri = &sInterp[s];
        PlayerState  *p  = &gameState.players[s];
        if (!p->active || ri->count < 2) continue;

        // Walk the ring buffer (newest → oldest) to find the two snapshots
        // that bracket renderTime.
        RemoteSnapshot *newer = NULL, *older = NULL;
        for (int i = 0; i < ri->count; i++) {
            int idx = (ri->head - i + INTERP_BUF) & (INTERP_BUF - 1);
            RemoteSnapshot *snap = &ri->buf[idx];
            if (snap->timestamp <= renderTime) {
                older = snap;
                // The previous iteration (i-1) gave us the newer one
                int prevIdx = (ri->head - (i - 1) + INTERP_BUF) & (INTERP_BUF - 1);
                if (i > 0) newer = &ri->buf[prevIdx];
                break;
            }
        }

        if (!older || !newer) {
            // renderTime is outside our buffer — just snap to newest
            p->pos = ri->buf[ri->head].pos;
            p->yaw = ri->buf[ri->head].yaw;
            continue;
        }

        double span = newer->timestamp - older->timestamp;
        float  t    = (span > 0.0001) ? (float)((renderTime - older->timestamp) / span) : 1.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        p->pos = Vector3Lerp(older->pos, newer->pos, t);

        // Lerp yaw via shortest arc to avoid spinning through 180
        float dy = newer->yaw - older->yaw;
        while (dy >  PI) dy -= 2.0f * PI;
        while (dy < -PI) dy += 2.0f * PI;
        p->yaw = older->yaw + dy * t;
    }
}

// ─────────────────────────────────────────────────────────────
// Client-side prediction state
// ─────────────────────────────────────────────────────────────

#define INPUT_HISTORY 128

typedef struct {
    InputMsg    msg;
    PlayerState stateBefore; // local player state immediately before this input
} PredictedInput;

static PredictedInput sHistory[INPUT_HISTORY];
static uint32_t       sNextSeq    = 1;
static uint32_t       sLastAcked  = 0;

// ─────────────────────────────────────────────────────────────
// Connect / Disconnect
// ─────────────────────────────────────────────────────────────

bool ClientConnect(const char *hostAddr, uint16_t port, const char *playerName) {
    if (enet_initialize() != 0) {
        TraceLog(LOG_ERROR, "CLIENT: enet_initialize failed");
        return false;
    }

    sClient = enet_host_create(NULL, 1, NUM_CHANNELS, 0, 0);
    if (!sClient) {
        TraceLog(LOG_ERROR, "CLIENT: enet_host_create failed");
        return false;
    }

    ENetAddress addr;
    enet_address_set_host(&addr, hostAddr);
    addr.port = port;

    sServer = enet_host_connect(sClient, &addr, NUM_CHANNELS, 0);
    if (!sServer) {
        TraceLog(LOG_ERROR, "CLIENT: enet_host_connect failed");
        return false;
    }

    // Wait up to 3 seconds for the TCP-level connection
    ENetEvent event;
    if (enet_host_service(sClient, &event, 3000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        TraceLog(LOG_INFO, "CLIENT: connected to %s:%d", hostAddr, port);
    } else {
        TraceLog(LOG_ERROR, "CLIENT: connection to %s:%d timed out", hostAddr, port);
        enet_peer_reset(sServer);
        return false;
    }

    // Send join request
    JoinReqMsg req = { .type = MSG_JOIN_REQ };
    strncpy(req.name, playerName, sizeof(req.name) - 1);
    ENetPacket *pkt = enet_packet_create(&req, sizeof(req), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(sServer, CHAN_RELIABLE, pkt);
    enet_host_flush(sClient);

    // Wait for JOIN_ACK — poll for up to 5 seconds
    double deadline = GetTime() + 5.0;
    while (GetTime() < deadline) {
        while (enet_host_service(sClient, &event, 50) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                uint8_t *data = event.packet->data;
                if (data[0] == MSG_JOIN_ACK && event.packet->dataLength >= sizeof(JoinAckMsg)) {
                    JoinAckMsg *ack = (JoinAckMsg *)data;
                    localSlot = ack->slot;
                    TraceLog(LOG_INFO, "CLIENT: assigned slot %d, map=%s",
                             localSlot, ack->mapName);

                    // Load the level the server specified
                    LoadLevel(ack->mapName);
                    InitCamera();

                    sConnected = true;
                    enet_packet_destroy(event.packet);
                    return true;
                }
                enet_packet_destroy(event.packet);
            }
        }
    }

    TraceLog(LOG_ERROR, "CLIENT: no JOIN_ACK received");
    return false;
}

void ClientDisconnect(void) {
    if (sServer) enet_peer_disconnect(sServer, 0);
    if (sClient) { enet_host_destroy(sClient); sClient = NULL; }
    sConnected = false;
    enet_deinitialize();
}

bool ClientIsConnected(void) { return sConnected; }

// ─────────────────────────────────────────────────────────────
// Reconciliation
// ─────────────────────────────────────────────────────────────

// Called when we receive a snapshot with lastInputAck > sLastAcked.
// Rewinds local player to the server's authoritative position, then
// replays all inputs the server hasn't processed yet.
static void Reconcile(const SnapshotMsg *snap) {
    uint32_t ack = snap->lastInputAck;
    if (ack <= sLastAcked) return; // nothing new
    sLastAcked = ack;

    const NetPlayer *np = &snap->players[localSlot];
    PlayerState *local  = &gameState.players[localSlot];

    // Reset to server-confirmed position
    local->pos.x = np->x;
    local->pos.y = np->y;
    local->pos.z = np->z;
    local->yaw   = np->yaw;
    local->health = np->health;

    // Replay inputs the server hasn't acked yet
    for (uint32_t seq = ack + 1; seq < sNextSeq; seq++) {
        PredictedInput *pi = &sHistory[seq % INPUT_HISTORY];
        if (pi->msg.sequence == seq) {
            SimulatePlayer(local, &pi->msg, pi->msg.dt);
        }
    }
}

// ─────────────────────────────────────────────────────────────
// ClientPollNetwork
// ─────────────────────────────────────────────────────────────

void ClientPollNetwork(void) {
    if (!sClient) return;

    ENetEvent event;
    while (enet_host_service(sClient, &event, 0) > 0) {
        switch (event.type) {

        case ENET_EVENT_TYPE_RECEIVE: {
            uint8_t *data = event.packet->data;
            size_t   len  = event.packet->dataLength;

            switch (data[0]) {
            case MSG_SNAPSHOT: {
                if (len < sizeof(SnapshotMsg)) break;
                SnapshotMsg *snap = (SnapshotMsg *)data;

                // Push snapshot positions into interpolation ring buffers.
                // ClientInterpolateRemote() lerps between samples each frame.
                double now = GetTime();
                for (int s = 0; s < MAX_PLAYERS; s++) {
                    if (s == localSlot) continue; // handled by reconcile
                    PlayerState     *p  = &gameState.players[s];
                    const NetPlayer *np = &snap->players[s];

                    p->active = np->active;
                    p->health = np->health;

                    if (np->active)
                        InterpPush(s, (Vector3){ np->x, np->y, np->z }, np->yaw, now);
                }

                // Reconcile our own predicted position
                Reconcile(snap);
                break;
            }
            case MSG_EVENT: {
                if (len < sizeof(EventMsg)) break;
                EventMsg *ev = (EventMsg *)data;
                // TODO: hook into HUD/kill feed
                if (ev->eventType == EVENT_FRAG) {
                    TraceLog(LOG_INFO, "CLIENT: player %d fragged player %d",
                             ev->instigator, ev->target);
                }
                break;
            }
            default:
                break;
            }
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
            TraceLog(LOG_INFO, "CLIENT: disconnected from server");
            sConnected = false;
            sServer    = NULL;
            break;

        default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// ClientSendInput
// ─────────────────────────────────────────────────────────────

void ClientSendInput(float forwardAxis, float sideAxis,
                     float yaw, float pitch, uint8_t buttons, float dt) {
    if (!sConnected || !sServer) return;

    InputMsg msg = {
        .type        = MSG_INPUT,
        .sequence    = sNextSeq,
        .forwardAxis = forwardAxis,
        .sideAxis    = sideAxis,
        .yaw         = yaw,
        .pitch       = pitch,
        .buttons     = buttons,
        .dt          = dt,
    };

    // Save to history for reconciliation
    PredictedInput *pi = &sHistory[sNextSeq % INPUT_HISTORY];
    pi->msg         = msg;
    pi->stateBefore = gameState.players[localSlot];

    // Predict locally using actual frame dt, not a fixed timestep
    SimulatePlayer(&gameState.players[localSlot], &msg, dt);

    sNextSeq++;

    // Send unreliably — dropped packets are fine, next frame covers it
    ENetPacket *pkt = enet_packet_create(&msg, sizeof(msg), 0);
    enet_peer_send(sServer, CHAN_UNRELIABLE, pkt);
}
