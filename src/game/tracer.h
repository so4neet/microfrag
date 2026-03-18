#ifndef TRACER_H
#define TRACER_H

#include <raylib.h>

// ─────────────────────────────────────────────────────────────
// Bullet tracers
// Thin lines that fade out over ~0.1 seconds.
// ─────────────────────────────────────────────────────────────

// Spawn a tracer from origin to endpoint.
// hit=true makes the endpoint flash brighter (impact spark).
void TracerSpawn(Vector3 origin, Vector3 endpoint, bool hit);

// Update + draw all active tracers. Call inside BeginMode3D.
void TracerUpdate(float dt);

// ─────────────────────────────────────────────────────────────
// Screen shake
// ─────────────────────────────────────────────────────────────

// Add a screen shake impulse of given magnitude.
void ShakeAdd(float magnitude);

// Returns a camera with shake offset applied.
// Call instead of GetPlayerCamera and pass result to BeginMode3D.
Camera3D ShakeApply(Camera3D cam);

// Tick the shake decay. Call once per frame before ShakeApply.
void ShakeUpdate(float dt);

#endif // TRACER_H
