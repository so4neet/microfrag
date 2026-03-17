#include <stdio.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <GL/gl.h>
#include "version.h"
#include "globals.h"
#include "player/player.h"
#include "world/worldobject.h"
#include "game/game_state.h"
#include "game/game_logic.h"
#include "net/net_host.h"
#include "net/net_client.h"

// ─────────────────────────────────────────────────────────────
// Global game state (definition — declared extern in game_state.h)
// ─────────────────────────────────────────────────────────────

GameState gameState = { 0 };
int       localSlot = 0;    // 0 = host, 1-7 = remote client

// ─────────────────────────────────────────────────────────────
// Remote player rendering
// ─────────────────────────────────────────────────────────────

// Called inside BeginMode3D — draws capsule bodies only.
static void DrawRemotePlayerBodies(void) {
    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (s == localSlot) continue;
        PlayerState *p = &gameState.players[s];
        if (!p->active) continue;
        float capRadius   = 0.3f;
        float groundY     = p->pos.y - playerHeight + capRadius;
        Vector3 bottom = { p->pos.x, groundY,        p->pos.z };
        Vector3 top    = { p->pos.x, groundY + 0.9f, p->pos.z };
        DrawCapsule(bottom, top, capRadius, 6, 4, RED);
    }
}

// Called after EndMode3D — projects name labels to screen space and draws 2D.
static void DrawRemotePlayerLabels(Camera3D cam) {
    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (s == localSlot) continue;
        PlayerState *p = &gameState.players[s];
        if (!p->active) continue;
        Vector3 labelPos  = { p->pos.x, p->pos.y - playerHeight + 1.0f, p->pos.z };
        Vector2 screenPos = GetWorldToScreen(labelPos, cam);
        int     textW     = MeasureText(p->name, 18);
        DrawText(p->name, (int)screenPos.x - textW / 2, (int)screenPos.y, 18, WHITE);
    }
}

// ─────────────────────────────────────────────────────────────
// Usage
// ─────────────────────────────────────────────────────────────

static void PrintUsage(const char *exe) {
    printf("Usage:\n");
    printf("  Host:   %s --host [mapfile]\n", exe);
    printf("  Client: %s --join <server_ip> [playername]\n", exe);
    printf("  Solo:   %s [mapfile]\n", exe);
}

// ─────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    bool isHosting  = false;
    bool isJoining  = false;
    char mapFile[128]    = "assets/world/brush_test2.wrld";
    char serverIP[64]    = "127.0.0.1";
    char playerName[16]  = "Player";

    // ── Argument parsing ──────────────────────────────────────
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0) {
            isHosting = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                strncpy(mapFile, argv[++i], sizeof(mapFile) - 1);
            }
        } else if (strcmp(argv[i], "--join") == 0) {
            isJoining = true;
            if (i + 1 < argc) strncpy(serverIP,   argv[++i], sizeof(serverIP) - 1);
            if (i + 1 < argc) strncpy(playerName, argv[++i], sizeof(playerName) - 1);
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]); return 0;
        } else {
            // Bare argument = map file (solo or legacy usage)
            strncpy(mapFile, argv[i], sizeof(mapFile) - 1);
        }
    }

    // ── Window ────────────────────────────────────────────────
    InitWindow(640, 480, "Vital");
    SetTargetFPS(165);
    DisableCursor();

    printf("GL Vendor:   %s\n", glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL Version:  %s\n", glGetString(GL_VERSION));

    rlDisableBackfaceCulling();

    // ── Network + level init ──────────────────────────────────
    if (isHosting) {
        // Host: load level locally, then start listening
        LoadLevel(mapFile);
        localSlot = 0;
        RespawnPlayer(&gameState.players[0]);  // sets pos from spawnPoint, health=100, active=true

        if (!HostInit(mapFile)) {
            TraceLog(LOG_ERROR, "Failed to start host");
            CloseWindow();
            return 1;
        }
    } else if (isJoining) {
        // Client: level is loaded after JOIN_ACK, inside ClientConnect()
        localSlot = -1; // will be set by ClientConnect
        if (!ClientConnect(serverIP, NET_PORT, playerName)) {
            TraceLog(LOG_ERROR, "Failed to connect to %s", serverIP);
            CloseWindow();
            return 1;
        }
    } else {
        // Solo (original behaviour)
        LoadLevel(mapFile);
        localSlot = 0;
        RespawnPlayer(&gameState.players[0]);
    }

    InitCamera();

    // ── Game loop ─────────────────────────────────────────────
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Server-side work (host only)
        if (isHosting) {
            HostPollNetwork();
            HostTick(dt);
            HostBroadcastSnapshot();
        }

        // Client-side network receive (joining client only)
        if (isJoining) {
            ClientPollNetwork();
            ClientInterpolateRemote();
        }

        // Local player input + prediction
        HandlePlayer();
        HandleGravity(); // stub, kept for API compatibility

        // Sync host's own position into gameState slot 0 each frame
        if (isHosting || (!isHosting && !isJoining)) {
            // player.c already wrote into gameState.players[localSlot]
            // via SimulatePlayer; nothing extra needed here.
        }

        Camera3D cam = GetPlayerCamera();
        BeginDrawing();
            ClearBackground(BLACK);
            BeginMode3D(cam);
                DrawWorld();
                DrawRemotePlayerBodies();
            EndMode3D();
            // Name labels drawn after EndMode3D as 2D overlays
            DrawRemotePlayerLabels(cam);

            // HUD
            DrawText("BUILD: " VERSION, 10, 10, 20, WHITE);
            if (isHosting)       DrawText("HOST",      10, 30, 20, GREEN);
            else if (isJoining)  DrawText("CLIENT",    10, 30, 20, SKYBLUE);
            else                 DrawText("SOLO",      10, 30, 20, GRAY);

            // Health
            char hud[32];
            snprintf(hud, sizeof(hud), "HP: %d",
                     gameState.players[localSlot].health);
            DrawText(hud, 10, 50, 20, WHITE);

            DrawFPS(10, 360);
        EndDrawing();
    }

    // ── Cleanup ───────────────────────────────────────────────
    if (isHosting)  HostShutdown();
    if (isJoining)  ClientDisconnect();

    CloseWindow();
    return 0;
}
