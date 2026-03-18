#include <stdio.h>
#include <string.h>
#include <raylib.h>
#include "net_host.h"
#include "net.h"
#include "../game/game_state.h"
#include "../game/game_logic.h"
#include "../game/weapon.h"
#include "../audio/audio.h"

static ENetHost *sHost    = NULL;
static ENetPeer *sPeers[MAX_CLIENTS] = { 0 };  // sPeers[i] → slot i+1
static char      sMapName[64]        = { 0 };

// Snapshot rate limiting
static double sLastSnapshotTime = 0.0;
static const double kSnapshotInterval = 1.0 / SNAPSHOT_HZ;

// ─────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────

bool HostInit(const char *mapName) {
    if (enet_initialize() != 0) {
        TraceLog(LOG_ERROR, "HOST: enet_initialize failed");
        return false;
    }

    ENetAddress addr = { .host = ENET_HOST_ANY, .port = NET_PORT };
    sHost = enet_host_create(&addr, MAX_CLIENTS, NUM_CHANNELS, 0, 0);
    if (!sHost) {
        TraceLog(LOG_ERROR, "HOST: enet_host_create failed");
        return false;
    }

    strncpy(sMapName, mapName, sizeof(sMapName) - 1);
    TraceLog(LOG_INFO, "HOST: listening on port %d, map=%s", NET_PORT, sMapName);
    return true;
}

void HostShutdown(void) {
    if (sHost) { enet_host_destroy(sHost); sHost = NULL; }
    enet_deinitialize();
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

// Find a free slot (1..MAX_CLIENTS). Returns -1 if full.
static int FindFreeSlot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!sPeers[i]) return i + 1;  // slot 0 is the host
    return -1;
}

static void SendReliable(ENetPeer *peer, const void *data, size_t len) {
    ENetPacket *pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, CHAN_RELIABLE, pkt);
}

static void SendUnreliable(ENetPeer *peer, const void *data, size_t len) {
    ENetPacket *pkt = enet_packet_create(data, len, 0);
    enet_peer_send(peer, CHAN_UNRELIABLE, pkt);
}

// ─────────────────────────────────────────────────────────────
// Packet handlers
// ─────────────────────────────────────────────────────────────

static void HandleJoinReq(ENetPeer *peer, const uint8_t *data, size_t len) {
    if (len < sizeof(JoinReqMsg)) return;
    const JoinReqMsg *req = (const JoinReqMsg *)data;

    int slot = FindFreeSlot();
    if (slot < 0) {
        TraceLog(LOG_WARNING, "HOST: server full, rejecting client");
        enet_peer_disconnect(peer, 0);
        return;
    }

    // Register peer
    sPeers[slot - 1]    = peer;
    peer->data          = (void *)(uintptr_t)slot;

    // Initialise authoritative player state for this slot
    PlayerState *p = &gameState.players[slot];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, req->name, sizeof(p->name) - 1);
    RespawnPlayer(p);

    // Send acknowledgement — include all current player names
    JoinAckMsg ack = { .type = MSG_JOIN_ACK, .slot = (uint8_t)slot };
    strncpy(ack.mapName, sMapName, sizeof(ack.mapName) - 1);
    for (int s = 0; s < MAX_PLAYERS; s++)
        strncpy(ack.names[s], gameState.players[s].name,
                sizeof(ack.names[s]) - 1);
    SendReliable(peer, &ack, sizeof(ack));

    TraceLog(LOG_INFO, "HOST: '%s' joined as slot %d", p->name, slot);

    // Broadcast updated name table to all existing clients
    NamesMsg nm = { .type = MSG_NAMES };
    for (int s = 0; s < MAX_PLAYERS; s++)
        strncpy(nm.names[s], gameState.players[s].name, sizeof(nm.names[s]) - 1);
    HostBroadcastRaw(&nm, sizeof(nm), CHAN_RELIABLE);

    // Announce spawn to everyone
    HostBroadcastEvent(EVENT_SPAWN, (uint8_t)slot, (uint8_t)slot, 100);
}

static void HandleInput(ENetPeer *peer, const uint8_t *data, size_t len) {
    if (len < sizeof(InputMsg)) return;
    const InputMsg *msg = (const InputMsg *)data;

    int slot = (int)(uintptr_t)peer->data;
    if (slot < 1 || slot >= MAX_PLAYERS) return;

    PlayerState *p = &gameState.players[slot];
    if (!p->active) return;

    // Ignore out-of-order packets
    if (msg->sequence <= p->lastInputSeq && p->lastInputSeq != 0) return;
    p->lastInputSeq = msg->sequence;

    // Queue input — HostTick() will consume it this frame
    // For simplicity we apply it immediately here; a proper implementation
    // would buffer inputs and replay them in HostTick at a fixed timestep.
    // That's sufficient for a first pass.
    SimulatePlayer(p, msg, msg->dt);

    // Weapon switch
    if (msg->buttons & BTN_NEXT_WEP) WeaponCycleNext(slot);
    if (msg->buttons & BTN_PREV_WEP) WeaponCyclePrev(slot);

    // Tick cooldown and fire — slot 0 handled by player.c
    if (slot > 0) {
        WeaponTickCooldown(slot, msg->dt);
        if (msg->buttons & BTN_FIRE) WeaponTryFire(slot, msg->dt);
    }
}

// ─────────────────────────────────────────────────────────────
// HostPollNetwork
// ─────────────────────────────────────────────────────────────

void HostPollNetwork(void) {
    if (!sHost) return;

    ENetEvent event;
    while (enet_host_service(sHost, &event, 0) > 0) {
        switch (event.type) {

        case ENET_EVENT_TYPE_CONNECT:
            TraceLog(LOG_INFO, "HOST: peer connecting from %x:%u",
                     event.peer->address.host, event.peer->address.port);
            event.peer->data = (void *)(uintptr_t)(-1); // slot not yet assigned
            break;

        case ENET_EVENT_TYPE_RECEIVE: {
            if (event.packet->dataLength < 1) { enet_packet_destroy(event.packet); break; }
            uint8_t *data = event.packet->data;
            size_t   len  = event.packet->dataLength;

            switch (data[0]) {
            case MSG_JOIN_REQ: HandleJoinReq(event.peer, data, len); break;
            case MSG_INPUT:    HandleInput(event.peer, data, len);   break;
            default:
                TraceLog(LOG_WARNING, "HOST: unknown msg type %d", data[0]);
                break;
            }
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT: {
            int slot = (int)(uintptr_t)event.peer->data;
            if (slot >= 1 && slot < MAX_PLAYERS) {
                TraceLog(LOG_INFO, "HOST: slot %d disconnected", slot);
                gameState.players[slot].active = false;
                sPeers[slot - 1] = NULL;
                HostBroadcastEvent(EVENT_FRAG, 0, (uint8_t)slot, 0);
            }
            event.peer->data = (void *)(uintptr_t)(-1);
            break;
        }

        default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// HostTick — authoritative sim for remote players
// ─────────────────────────────────────────────────────────────

#define RESPAWN_DELAY 3.0f

void HostTick(float dt) {
    gameState.tick++;

    // Tick respawn timers for all players
    for (int s = 0; s < MAX_PLAYERS; s++) {
        PlayerState *p = &gameState.players[s];
        if (!p->active && p->respawnTimer > 0.0f) {
            p->respawnTimer -= dt;
            if (p->respawnTimer <= 0.0f) {
                RespawnPlayer(p);
                HostBroadcastEvent(EVENT_SPAWN, (uint8_t)s, (uint8_t)s, 100);
                AudioPlaySpawn(s == 0);
                TraceLog(LOG_INFO, "HOST: player %d respawned", s);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// HostBroadcastSnapshot
// ─────────────────────────────────────────────────────────────

void HostBroadcastSnapshot(void) {
    if (!sHost) return;

    double now = GetTime();
    if (now - sLastSnapshotTime < kSnapshotInterval) return;
    sLastSnapshotTime = now;

    // Build the snapshot (slot 0 = host player, filled by player.c via gameState)
    SnapshotMsg snap;
    snap.type       = MSG_SNAPSHOT;
    snap.serverTick = gameState.tick;

    for (int s = 0; s < MAX_PLAYERS; s++) {
        PlayerState *p = &gameState.players[s];
        snap.players[s].active   = p->active ? 1 : 0;
        snap.players[s].x        = p->pos.x;
        snap.players[s].y        = p->pos.y;
        snap.players[s].z        = p->pos.z;
        snap.players[s].yaw      = p->yaw;
        snap.players[s].health   = (int16_t)p->health;
        snap.players[s].weapon   = p->weapon;
        snap.players[s].vx       = p->horizVel.x;
        snap.players[s].vz       = p->horizVel.z;
        snap.players[s].vy       = p->vertVelocity;
        snap.players[s].grounded = p->grounded ? 1 : 0;
    }

    // Send a personalised copy to each client (lastInputAck differs per peer)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!sPeers[i]) continue;
        int slot = i + 1;
        snap.lastInputAck = gameState.players[slot].lastInputSeq;
        SendUnreliable(sPeers[i], &snap, sizeof(snap));
    }
}

// ─────────────────────────────────────────────────────────────
// HostBroadcastEvent
// ─────────────────────────────────────────────────────────────

void HostBroadcastRaw(const void *data, size_t len, int channel) {
    if (!sHost) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!sPeers[i]) continue;
        ENetPacket *pkt = enet_packet_create(data, len,
            channel == CHAN_RELIABLE ? ENET_PACKET_FLAG_RELIABLE : 0);
        enet_peer_send(sPeers[i], channel, pkt);
    }
}

void HostBroadcastEvent(uint8_t eventType, uint8_t instigator,
                        uint8_t target, int16_t value) {
    if (!sHost) return;
    EventMsg ev = {
        .type       = MSG_EVENT,
        .eventType  = eventType,
        .instigator = instigator,
        .target     = target,
        .value      = value,
    };
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!sPeers[i]) continue;
        SendReliable(sPeers[i], &ev, sizeof(ev));
    }
}
