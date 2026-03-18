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
#include "game/weapon.h"
#include "game/tracer.h"
#include "net/net_host.h"
#include "net/net_client.h"
#include "audio/audio.h"
#include "skybox/skybox.h"
#include "menu/menu.h"

// ─────────────────────────────────────────────────────────────
// Global game state (definition — declared extern in game_state.h)
// ─────────────────────────────────────────────────────────────

GameState gameState = { 0 };
int       localSlot = 0;    // 0 = host, 1-7 = remote client

// ─────────────────────────────────────────────────────────────
// Remote player rendering
// ─────────────────────────────────────────────────────────────

// Called inside BeginMode3D — draws capsule bodies + held weapon world model.
static void DrawRemotePlayerBodies(void) {
    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (s == localSlot) continue;
        PlayerState *p = &gameState.players[s];
        if (!p->active) continue;

        float capRadius = 0.3f;
        float groundY   = p->pos.y - playerHeight + capRadius;
        Vector3 bottom  = { p->pos.x, groundY,        p->pos.z };
        Vector3 top     = { p->pos.x, groundY + 0.9f, p->pos.z };
        DrawCapsule(bottom, top, capRadius, 6, 4, RED);

        // World model — held at the player's right hand position
        Model *worldModel = WeaponGetWorldModel(p->weapon);
        Vector3 handPos = {
            p->pos.x + cosf(p->yaw) * 0.3f,
            p->pos.y - 0.3f,
            p->pos.z - sinf(p->yaw) * 0.3f,
        };
        DrawModelEx(*worldModel, handPos,
                    (Vector3){0,1,0}, p->yaw * RAD2DEG + 180.0f,
                    (Vector3){1,1,1}, WHITE);
    }
}

// Called after EndMode3D — projects name labels to screen space and draws 2D.
static void DrawRemotePlayerLabels(Camera3D cam) {
    Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (s == localSlot) continue;
        PlayerState *p = &gameState.players[s];
        if (!p->active) continue;
        Vector3 labelPos = { p->pos.x, p->pos.y - playerHeight + 1.0f, p->pos.z };

        // Skip if the label is behind the camera — GetWorldToScreen wraps
        // behind-camera points to the opposite side of the screen.
        Vector3 toLabel = Vector3Subtract(labelPos, cam.position);
        if (Vector3DotProduct(toLabel, camFwd) <= 0.0f) continue;

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
    (void)argc; (void)argv;

    // ── Menu ──────────────────────────────────────────────────
    // RunMenu opens its own temporary window, lets the player choose
    // host/join/settings, then closes it. We then open the game window
    // at the resolution they picked.
    MenuChoice choice = { 0 };
    MenuResult menuResult = RunMenu(&choice);
    if (menuResult == MENU_QUIT) return 0;

    bool isHosting = (menuResult == MENU_HOST);
    bool isJoining = (menuResult == MENU_JOIN);
    char mapFile[128],  serverIP[64], playerName[16];
    strncpy(mapFile,    choice.mapFile,    sizeof(mapFile)    - 1);
    strncpy(serverIP,   choice.serverIP,   sizeof(serverIP)   - 1);
    strncpy(playerName, choice.playerName, sizeof(playerName) - 1);

    // ── Window (game resolution from settings) ────────────────
    InitWindow(gSettings.resWidth, gSettings.resHeight, "Vital");
    SetTargetFPS(165);
    DisableCursor();

    printf("GL Vendor:   %s\n", glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL Version:  %s\n", glGetString(GL_VERSION));

    rlDisableBackfaceCulling();
    InitAudioDevice();
    SetMasterVolume(gSettings.volume);

    // ── Network + level init ──────────────────────────────────
    if (isHosting) {
        // Host: load level locally, then start listening
        LoadLevel(mapFile);
        localSlot = 0;
        RespawnPlayer(&gameState.players[0]);
        strncpy(gameState.players[0].name, playerName, sizeof(gameState.players[0].name) - 1);

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
        strncpy(gameState.players[0].name, playerName, sizeof(gameState.players[0].name) - 1);
    }

    InitCamera();
    WeaponsInit();
    AudioInit();
    SkyboxInit();

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

        // Tick local respawn timer for HUD display
        if (!gameState.players[localSlot].active &&
             gameState.players[localSlot].respawnTimer > 0.0f)
            gameState.players[localSlot].respawnTimer -= dt;

        // Local player input + prediction
        HandlePlayer();
        HandleGravity(); // stub, kept for API compatibility

        // Sync host's own position into gameState slot 0 each frame
        if (isHosting || (!isHosting && !isJoining)) {
            // player.c already wrote into gameState.players[localSlot]
            // via SimulatePlayer; nothing extra needed here.
        }

        // Update shake and get shaken camera
        AudioUpdate();
        ShakeUpdate(dt);
        Camera3D cam = ShakeApply(GetPlayerCamera());

        BeginDrawing();
            ClearBackground(BLACK);
            BeginMode3D(cam);
                SkyboxDraw(cam);
                DrawWorld();
                DrawRemotePlayerBodies();
                TracerUpdate(dt);
            EndMode3D();

            // ── Viewmodel (separate 3D pass — never clips into walls)
            {
                Camera3D vmCam = cam;
                vmCam.fovy = 70.0f;
                Model *vm = WeaponGetViewModel(gameState.players[localSlot].weapon);
                BeginMode3D(vmCam);
                    // Camera basis — fwd encodes full pitch+yaw
                    Vector3 fwd = Vector3Normalize(
                        Vector3Subtract(cam.target, cam.position));
                    Vector3 rgt = Vector3Normalize(
                        Vector3CrossProduct(fwd, (Vector3){0,1,0}));
                    Vector3 upv = Vector3CrossProduct(rgt, fwd);

                    // View bob
                    float speed = Vector3Length(gameState.players[localSlot].horizVel);
                    float bob   = sinf((float)GetTime() * 12.0f)
                                  * (speed / moveSpeed) * 0.012f;

                    // Gun position in camera space
                    Vector3 vmPos = Vector3Add(cam.position,
                        Vector3Add(Vector3Scale(fwd,  0.35f),
                        Vector3Add(Vector3Scale(rgt,  0.12f),
                                   Vector3Scale(upv, -0.10f + bob))));

                    // Use the GL matrix stack directly.
                    // WSHAPE_BOX:  barrel along local +X → map +X to fwd
                    // WSHAPE_TUBE: barrel along local +Z → map +Z to fwd
                    uint8_t wid = gameState.players[localSlot].weapon;
                    float m[16];
                    if (weapons[wid].shape == WSHAPE_BOX) {
                        // local +X → fwd, +Y → upv, +Z → rgt
                        m[0]=fwd.x; m[1]=fwd.y; m[2]=fwd.z; m[3]=0;
                        m[4]=upv.x; m[5]=upv.y; m[6]=upv.z; m[7]=0;
                        m[8]=rgt.x; m[9]=rgt.y; m[10]=rgt.z;m[11]=0;
                    } else {
                        // local +Z → fwd, +Y → upv, +X → -rgt
                        m[0]=-rgt.x;m[1]=-rgt.y;m[2]=-rgt.z;m[3]=0;
                        m[4]=upv.x; m[5]=upv.y; m[6]=upv.z; m[7]=0;
                        m[8]=fwd.x; m[9]=fwd.y; m[10]=fwd.z;m[11]=0;
                    }
                    m[12]=vmPos.x; m[13]=vmPos.y; m[14]=vmPos.z; m[15]=1;

                    rlDisableDepthTest();
                    rlPushMatrix();
                        rlMultMatrixf(m);
                        // Draw with identity transform — position/rotation is on the stack
                        for (int mi = 0; mi < vm->meshCount; mi++)
                            DrawMesh(vm->meshes[mi],
                                     vm->materials[vm->meshMaterial[mi]],
                                     MatrixIdentity());
                    rlPopMatrix();
                    rlEnableDepthTest();
                EndMode3D();
            }

            // Name labels drawn after EndMode3D as 2D overlays
            DrawRemotePlayerLabels(cam);

            // HUD
            DrawText("BUILD: " VERSION, 10, 10, 20, WHITE);
            if (isHosting)       DrawText("HOST",      10, 30, 20, GREEN);
            else if (isJoining)  DrawText("CLIENT",    10, 30, 20, SKYBLUE);
            else                 DrawText("SOLO",      10, 30, 20, GRAY);

            // Health / respawn HUD
            PlayerState *localP = &gameState.players[localSlot];
            if (localP->active) {
                char hud[32];
                snprintf(hud, sizeof(hud), "HP: %d", localP->health);
                DrawText(hud, 10, 50, 20, WHITE);
                // Weapon name
                DrawText(weapons[localP->weapon].name, 10, 75, 18, GRAY);
            } else {
                // Dead — show respawn countdown
                char msg[48];
                int secs = (int)localP->respawnTimer + 1;
                snprintf(msg, sizeof(msg), "RESPAWNING IN %d...", secs);
                int tw = MeasureText(msg, 28);
                DrawText(msg,
                         GetScreenWidth()/2 - tw/2,
                         GetScreenHeight()/2 - 14,
                         28, RED);
            }

            DrawFPS(10, 360);
        EndDrawing();
    }

    // ── Cleanup ───────────────────────────────────────────────
    SkyboxShutdown();
    AudioShutdown();
    WeaponsShutdown();
    if (isHosting)  HostShutdown();
    if (isJoining)  ClientDisconnect();

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
