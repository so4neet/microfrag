#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "../globals.h"
#include "../world/worldobject.h"
#include "../game/game_state.h"
#include "../net/net_client.h"

static Camera3D camera      = { 0 };
static float    bobbingTime = 0.0f;
static float    headBobOffset = 0.0f;

static float sYaw   = 0.0f;  // accumulated look angles, kept here for input
static float sPitch = 0.0f;

const float bobFreq   = 6.0f;
const float bobAmount = 0.05f;

void InitCamera(void) {
    camera.position   = (Vector3){ spawnPoint.x, spawnPoint.y + playerHeight, spawnPoint.z };
    camera.target     = (Vector3){ spawnPoint.x, spawnPoint.y, spawnPoint.z + 1.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Sync local player slot into gameState so game_logic sees it
    PlayerState *local = &gameState.players[localSlot];
    local->active = true;
    local->pos    = spawnPoint;
    local->pos.y += playerHeight;
    local->health = 100;
}

// ─────────────────────────────────────────────────────────────
// Sync camera to the local player's authoritative/predicted state
// ─────────────────────────────────────────────────────────────

static void SyncCameraToState(void) {
    PlayerState *local = &gameState.players[localSlot];

    // Camera position comes from physics state
    camera.position = local->pos;

    // Rebuild target from yaw/pitch.
    // GetMouseDelta().y is positive downward, so += on sPitch makes mouse-down = positive pitch.
    // We negate sPitch in the target so positive pitch = look up.
    float cp = cosf(sPitch);
    camera.target = (Vector3){
        local->pos.x + sinf(sYaw) * cp,
        local->pos.y - sinf(sPitch),
        local->pos.z + cosf(sYaw) * cp,
    };
}

// ─────────────────────────────────────────────────────────────
// HandlePlayer — gather input, drive local player
// ─────────────────────────────────────────────────────────────

void HandlePlayer(void) {
    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;

    if (!isLevelLoaded) return;

    // ── 1. Mouse look ─────────────────────────────────────────
    Vector2 mouseDelta = GetMouseDelta();
    sYaw   -= mouseDelta.x * 0.002f;
    sPitch += mouseDelta.y * 0.002f;
    if (sPitch >  1.4f) sPitch =  1.4f; // clamp ~80 degrees
    if (sPitch < -1.4f) sPitch = -1.4f;

    // ── 2. Keyboard movement axes ─────────────────────────────
    float forwardAxis = 0.0f, sideAxis = 0.0f;
    bool  isMoving    = false;

    if (IsKeyDown(KEY_W)) { forwardAxis += 1.0f; isMoving = true; }
    if (IsKeyDown(KEY_S)) { forwardAxis -= 1.0f; isMoving = true; }
    if (IsKeyDown(KEY_D)) { sideAxis    += 1.0f; isMoving = true; }
    if (IsKeyDown(KEY_A)) { sideAxis    -= 1.0f; isMoving = true; }

    uint8_t buttons = 0;
    if (IsKeyPressed(KEY_SPACE)) buttons |= BTN_JUMP;
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) buttons |= BTN_FIRE;

    // ── 3. Drive movement ─────────────────────────────────────
    // If we're a connected client, send input to server and predict locally.
    // If we're the host (localSlot == 0), simulate directly — no network hop.
    if (ClientIsConnected()) {
        ClientSendInput(forwardAxis, sideAxis, sYaw, sPitch, buttons, dt);
    } else {
        // Host player: build a synthetic InputMsg and run SimulatePlayer
        // so all movement goes through the same deterministic code path.
        InputMsg input = {
            .forwardAxis = forwardAxis,
            .sideAxis    = sideAxis,
            .yaw         = sYaw,
            .pitch       = sPitch,
            .buttons     = buttons,
        };
        extern void SimulatePlayer(PlayerState *, const InputMsg *, float);
        SimulatePlayer(&gameState.players[localSlot], &input, dt);
    }

    // ── 4. Head bob ───────────────────────────────────────────
    if (isMoving && gameState.players[localSlot].grounded) {
        bobbingTime   += dt * bobFreq;
        headBobOffset  = sinf(bobbingTime) * bobAmount;
    } else {
        headBobOffset = 0.0f;
        bobbingTime   = 0.0f;
    }

    SyncCameraToState();
}

// ─────────────────────────────────────────────────────────────
// HandleGravity — kept as a no-op stub; gravity now lives in
// SimulatePlayer (game_logic.c) so it runs identically on host
// and client. This function is called from main.c for compatibility.
// ─────────────────────────────────────────────────────────────

void HandleGravity(void) {
    // Intentionally empty: gravity is applied inside SimulatePlayer().
}

// ─────────────────────────────────────────────────────────────
// GetPlayerCamera
// ─────────────────────────────────────────────────────────────

Camera3D GetPlayerCamera(void) {
    Camera3D vis  = camera;
    vis.position.y += headBobOffset;
    vis.target.y   += headBobOffset;
    return vis;
}
