#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "../globals.h"
#include "../world/worldobject.h"
#include "../game/game_state.h"
#include "../net/net_client.h"
#include "../menu/menu.h"
#include "../game/weapon.h"
#include "../game/tracer.h"
#include "../audio/audio.h"

static Camera3D camera      = { 0 };
static float    bobbingTime = 0.0f;
static float    headBobOffset = 0.0f;

static float sYaw        = 0.0f;
static float sPitch      = 0.0f;
static float sRecoilPitch = 0.0f;  // decays back to 0 each frame

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

    // Copy pos to a local variable first — avoids potential aliasing issues
    // where the compiler might reorder reads of local->pos relative to writes
    // from Reconcile/SimulatePlayer earlier in the same frame.
    Vector3 pos = local->pos;
    camera.position = pos;

    // Decay recoil back to zero
    sRecoilPitch *= 0.85f;
    if (fabsf(sRecoilPitch) < 0.0001f) sRecoilPitch = 0.0f;

    float visualPitch = sPitch + sRecoilPitch;
    float cp = cosf(visualPitch);
    camera.target = (Vector3){
        pos.x + sinf(sYaw) * cp,
        pos.y - sinf(visualPitch),
        pos.z + cosf(sYaw) * cp,
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
    float sens = 0.002f * gSettings.sensitivity;
    sYaw   -= mouseDelta.x * sens;
    sPitch += mouseDelta.y * sens;
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
    if (IsKeyDown(KEY_SPACE))             buttons |= BTN_JUMP;
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) buttons |= BTN_FIRE;
    if (IsKeyPressed(KEY_E))              buttons |= BTN_NEXT_WEP;
    if (IsKeyPressed(KEY_Q))              buttons |= BTN_PREV_WEP;

    // Mouse wheel weapon cycle
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) buttons |= BTN_NEXT_WEP;
    if (wheel < 0.0f) buttons |= BTN_PREV_WEP;

    // ── 3. Drive movement ─────────────────────────────────────
    // If we're a connected client, send input to server and predict locally.
    // If we're the host (localSlot == 0), simulate directly — no network hop.
    if (ClientIsConnected()) {
        // Play shoot sound locally when the client fires — don't wait for
        // MSG_HIT round-trip. Check cooldown so it only fires once per shot.
        PlayerState *local = &gameState.players[localSlot];
        if ((buttons & BTN_FIRE) && local->fireCooldown <= 0.0f) {
            AudioPlayShoot(local->weapon);
            sRecoilPitch -= weapons[local->weapon].recoilPitch;
            ShakeAdd(weapons[local->weapon].screenShake);
            // Mirror the cooldown locally so sound doesn't repeat every frame
            local->fireCooldown = weapons[local->weapon].fireRate;
        }
        // Tick cooldown locally so the above check stays in sync
        WeaponTickCooldown(localSlot, dt);
        ClientSendInput(forwardAxis, sideAxis, sYaw, sPitch, buttons, dt);
    } else {
        // Host player: build a synthetic InputMsg and run SimulatePlayer
        InputMsg input = {
            .forwardAxis = forwardAxis,
            .sideAxis    = sideAxis,
            .yaw         = sYaw,
            .pitch       = sPitch,
            .buttons     = buttons,
        };
        extern void SimulatePlayer(PlayerState *, const InputMsg *, float);
        SimulatePlayer(&gameState.players[localSlot], &input, dt);

        // Weapon switch (host-side, immediate)
        if (buttons & BTN_NEXT_WEP) WeaponCycleNext(localSlot);
        if (buttons & BTN_PREV_WEP) WeaponCyclePrev(localSlot);

        // Tick and fire
        WeaponTickCooldown(localSlot, dt);
        if ((buttons & BTN_FIRE) && WeaponTryFire(localSlot, dt)) {
            const WeaponDef *wep = &weapons[gameState.players[localSlot].weapon];
            sRecoilPitch -= wep->recoilPitch;
            ShakeAdd(wep->screenShake);
            AudioPlayShoot(gameState.players[localSlot].weapon);
        }
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
