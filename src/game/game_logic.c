#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "game_logic.h"
#include "../globals.h"
#include "../world/worldobject.h"

extern Vector3 spawnPoint; // from worldobject.c

void SimulatePlayer(PlayerState *p, const InputMsg *input, float dt) {
    if (!p->active) return;
    if (dt > 0.1f) dt = 0.1f;

    // ── 1. Rotation ───────────────────────────────────────────
    // Client sends absolute yaw/pitch — just apply directly.
    p->yaw   = input->yaw;
    p->pitch = input->pitch;

    // ── 2. Horizontal movement ────────────────────────────────
    Vector3 forward = {  sinf(p->yaw), 0.0f,  cosf(p->yaw) };
    Vector3 right   = { -cosf(p->yaw), 0.0f,  sinf(p->yaw) };

    Vector3 moveStep = Vector3Add(
        Vector3Scale(forward, input->forwardAxis * moveSpeed * dt),
        Vector3Scale(right,   input->sideAxis    * moveSpeed * dt));

    // X axis — slide along walls
    if (moveStep.x != 0.0f) {
        float xOff = (moveStep.x > 0) ? playerRadius : -playerRadius;
        Vector3 testX = { p->pos.x + moveStep.x + xOff, p->pos.y, p->pos.z };
        if (!IsWallAt(testX, p->pos)) p->pos.x += moveStep.x;
    }

    // Z axis
    if (moveStep.z != 0.0f) {
        float zOff = (moveStep.z > 0) ? playerRadius : -playerRadius;
        Vector3 testZ = { p->pos.x, p->pos.y, p->pos.z + moveStep.z + zOff };
        if (!IsWallAt(testZ, p->pos)) p->pos.z += moveStep.z;
    }

    // ── 3. Gravity & grounding ────────────────────────────────
    float groundY = GetGroundHeight(p->pos) + playerHeight;

    if (p->pos.y <= groundY + maxStepHeight && p->vertVelocity <= 0.0f) {
        p->grounded     = true;
        p->pos.y        = Lerp(p->pos.y, groundY, 15.0f * dt);
        p->vertVelocity = 0.0f;
    } else {
        p->grounded      = false;
        p->vertVelocity -= gravity * dt;
    }

    // ── 4. Jump ───────────────────────────────────────────────
    if ((input->buttons & BTN_JUMP) && p->grounded) {
        p->vertVelocity = jumpForce;
        p->grounded     = false;
    }

    // ── 5. Vertical displacement ──────────────────────────────
    if (!p->grounded || p->vertVelocity > 0.0f) {
        p->pos.y += p->vertVelocity * dt;
    }
}

void RespawnPlayer(PlayerState *p) {
    p->pos          = spawnPoint;
    p->pos.y       += playerHeight;
    p->yaw          = 0.0f;
    p->pitch        = 0.0f;
    p->vertVelocity = 0.0f;
    p->grounded     = false;
    p->health       = 100;
    p->active       = true;
}
