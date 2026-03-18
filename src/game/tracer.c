#include <math.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include "tracer.h"

// ─────────────────────────────────────────────────────────────
// Tracers
// ─────────────────────────────────────────────────────────────

#define MAX_TRACERS     64
#define TRACER_LIFETIME 0.12f   // seconds until fully faded
#define TRACER_WIDTH    0.08f   // visual thickness

typedef struct {
    Vector3 origin;
    Vector3 endpoint;
    float   life;       // remaining lifetime (starts at TRACER_LIFETIME)
    bool    hit;        // true = show impact spark at endpoint
    bool    active;
} Tracer;

static Tracer sTracers[MAX_TRACERS];

void TracerSpawn(Vector3 origin, Vector3 endpoint, bool hit) {
    // Find a free slot, evict the oldest if full
    int oldest = 0;
    float minLife = 999.0f;
    for (int i = 0; i < MAX_TRACERS; i++) {
        if (!sTracers[i].active) {
            oldest = i;
            break;
        }
        if (sTracers[i].life < minLife) { minLife = sTracers[i].life; oldest = i; }
    }
    sTracers[oldest] = (Tracer){
        .origin   = origin,
        .endpoint = endpoint,
        .life     = TRACER_LIFETIME,
        .hit      = hit,
        .active   = true,
    };
}

void TracerUpdate(float dt) {
    for (int i = 0; i < MAX_TRACERS; i++) {
        Tracer *t = &sTracers[i];
        if (!t->active) continue;

        t->life -= dt;
        if (t->life <= 0.0f) { t->active = false; continue; }

        // Alpha fades from 1 → 0 over lifetime
        float alpha = t->life / TRACER_LIFETIME;

        // Tracer line: bright yellow-white core
        Color col = {
            255,
            (unsigned char)(200 + 55 * alpha),
            (unsigned char)(100 * alpha),
            (unsigned char)(255 * alpha),
        };
        DrawLine3D(t->origin, t->endpoint, col);

        // Impact spark: small sphere at hit point that fades fast
        if (t->hit && alpha > 0.5f) {
            float sparkAlpha = (alpha - 0.5f) * 2.0f;
            Color spark = { 255, 200, 80, (unsigned char)(255 * sparkAlpha) };
            DrawSphere(t->endpoint, 0.06f * sparkAlpha, spark);
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Screen shake
// ─────────────────────────────────────────────────────────────

static float sMagnitude = 0.0f;
static float sDecay     = 8.0f;  // how fast shake decays per second

void ShakeAdd(float magnitude) {
    sMagnitude += magnitude;
    // Cap so rapid fire doesn't make the screen permanently shaky
    if (sMagnitude > 0.4f) sMagnitude = 0.4f;
}

void ShakeUpdate(float dt) {
    sMagnitude -= sMagnitude * sDecay * dt;
    if (sMagnitude < 0.001f) sMagnitude = 0.0f;
}

Camera3D ShakeApply(Camera3D cam) {
    if (sMagnitude < 0.001f) return cam;

    // Simple random offset using fast sine hash — no stdlib rand needed
    static float sTime = 0.0f;
    sTime += 0.3f;
    float ox = sinf(sTime * 37.1f) * sMagnitude;
    float oy = sinf(sTime * 53.7f) * sMagnitude;

    cam.position.x += ox;
    cam.position.y += oy;
    cam.target.x   += ox;
    cam.target.y   += oy;
    return cam;
}
