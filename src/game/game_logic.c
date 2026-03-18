#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include "game_logic.h"
#include "../globals.h"
#include "../world/worldobject.h"

extern Vector3 spawnPoint;

// ─────────────────────────────────────────────────────────────
// Quake-style acceleration (Quakeworld PM_Accelerate)
//
// wishDir:   normalised direction the player wants to move
// wishSpeed: target speed in that direction (moveSpeed)
// accel:     acceleration constant (groundAccel or airAccel)
//
// Only adds velocity in wishDir up to wishSpeed — never subtracts.
// This is what allows air-strafing: you curve by holding a strafe
// key while turning the mouse, continuously accelerating toward a
// new wish direction that is slightly ahead of your current velocity.
// ─────────────────────────────────────────────────────────────

static Vector3 Accelerate(Vector3 vel, Vector3 wishDir, float wishSpeed,
                           float accel, float dt) {
    float currentSpeed = Vector3DotProduct(vel, wishDir);
    float addSpeed     = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return vel;

    float accelSpeed = accel * wishSpeed * dt;
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;

    return (Vector3){
        vel.x + accelSpeed * wishDir.x,
        vel.y + accelSpeed * wishDir.y,
        vel.z + accelSpeed * wishDir.z,
    };
}

// ─────────────────────────────────────────────────────────────
// Quake-style friction (Quakeworld PM_Friction)
//
// Applied only when grounded. Scales velocity down each frame
// proportional to speed, giving a smooth deceleration curve.
// The stopSpeed term means low-speed movement decelerates at a
// minimum rate so the player doesn't glide forever from tiny nudges.
// ─────────────────────────────────────────────────────────────

static Vector3 ApplyFriction(Vector3 vel, float dt) {
    float speed = Vector3Length(vel);
    if (speed < 0.01f) return (Vector3){ 0 };

    const float stopSpeed = 1.5f;
    float control  = (speed < stopSpeed) ? stopSpeed : speed;
    float drop     = control * friction * dt;
    float newSpeed = speed - drop;
    if (newSpeed < 0.0f) newSpeed = 0.0f;

    float scale = newSpeed / speed;
    return (Vector3){ vel.x * scale, vel.y * scale, vel.z * scale };
}

// ─────────────────────────────────────────────────────────────
// SimulatePlayer
// ─────────────────────────────────────────────────────────────

void SimulatePlayer(PlayerState *p, const InputMsg *input, float dt) {
    if (!p->active) return;
    if (dt > 0.1f) dt = 0.1f;

    // ── 1. Rotation ───────────────────────────────────────────
    p->yaw   = input->yaw;
    p->pitch = input->pitch;

    // ── 2. Wish direction from input ──────────────────────────
    Vector3 forward = {  sinf(p->yaw), 0.0f,  cosf(p->yaw) };
    Vector3 right   = { -cosf(p->yaw), 0.0f,  sinf(p->yaw) };

    Vector3 wishVel = Vector3Add(
        Vector3Scale(forward, input->forwardAxis),
        Vector3Scale(right,   input->sideAxis));

    float wishSpeed = 0.0f;
    Vector3 wishDir = { 0 };
    float wishLen = Vector3Length(wishVel);
    if (wishLen > 0.0f) {
        wishDir   = Vector3Scale(wishVel, 1.0f / wishLen);
        wishSpeed = moveSpeed;
    }

    // ── 3. Queue jump (bhop window) ───────────────────────────
    // Mirror the button state directly — jumpQueued is only true
    // while space is physically held, so releasing the key between
    // jumps clears it and prevents phantom double-jumps on landing.
    p->jumpQueued = (input->buttons & BTN_JUMP) != 0;

    // ── 4. Ground / air movement ──────────────────────────────
    if (p->grounded) {
        // Apply friction first, then accelerate (Quake order)
        p->horizVel = ApplyFriction(p->horizVel, dt);
        p->horizVel = Accelerate(p->horizVel, wishDir, wishSpeed, groundAccel, dt);

        // Bhop: jump immediately on landing if button is still held
        if (p->jumpQueued) {
            p->vertVelocity = jumpForce;
            p->grounded     = false;
            p->jumpQueued   = false;
        }
    } else {
        // Air acceleration — low constant makes strafing skillful
        p->horizVel = Accelerate(p->horizVel, wishDir, wishSpeed, airAccel, dt);

        // Cap bhop speed so it can't grow without bound
        if (maxBhopSpeed > 0.0f) {
            float spd = Vector3Length(p->horizVel);
            if (spd > maxBhopSpeed)
                p->horizVel = Vector3Scale(p->horizVel, maxBhopSpeed / spd);
        }
    }

    // ── 5. Apply horizontal velocity with wall sliding ────────
    Vector3 moveStep = Vector3Scale(p->horizVel, dt);

    if (moveStep.x != 0.0f) {
        float xOff    = (moveStep.x > 0) ? playerRadius : -playerRadius;
        Vector3 testX = { p->pos.x + moveStep.x + xOff, p->pos.y, p->pos.z };
        if (!IsWallAt(testX, p->pos)) {
            p->pos.x += moveStep.x;
        } else {
            p->horizVel.x = 0.0f;
        }
    }

    if (moveStep.z != 0.0f) {
        float zOff    = (moveStep.z > 0) ? playerRadius : -playerRadius;
        Vector3 testZ = { p->pos.x, p->pos.y, p->pos.z + moveStep.z + zOff };
        if (!IsWallAt(testZ, p->pos)) {
            p->pos.z += moveStep.z;
        } else {
            p->horizVel.z = 0.0f;
        }
    }

    // ── 6. Gravity & grounding ────────────────────────────────
    float groundY = GetGroundHeight(p->pos) + playerHeight;

    if (p->pos.y <= groundY + maxStepHeight && p->vertVelocity <= 0.0f) {
        p->grounded     = true;
        p->pos.y        = Lerp(p->pos.y, groundY, 15.0f * dt);
        p->vertVelocity = 0.0f;
    } else {
        p->grounded      = false;
        p->vertVelocity -= gravity * dt;
    }

    // ── 7. Vertical displacement ──────────────────────────────
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
    p->horizVel     = (Vector3){ 0 };
    p->jumpQueued   = false;
    p->grounded     = false;
    p->health       = 100;
    p->active       = true;
}
