#ifndef SKYBOX_H
#define SKYBOX_H

#include <raylib.h>
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────
// Skybox
//
// Supports two asset setups:
//
// A) Single panoramic HDR/PNG equirectangular image:
//      assets/skybox/sky.hdr   (or sky.png)
//   The image is converted to a cubemap internally.
//
// B) Six separate cube face images:
//      assets/skybox/right.png   assets/skybox/left.png
//      assets/skybox/top.png     assets/skybox/bottom.png
//      assets/skybox/front.png   assets/skybox/back.png
//
// If no assets are found, a simple gradient fallback is used.
// ─────────────────────────────────────────────────────────────

// Call once after InitWindow.
void SkyboxInit(void);

// Call once at shutdown.
void SkyboxShutdown(void);

// Call inside BeginMode3D, BEFORE DrawWorld.
// Disables depth write so the sky is always behind geometry.
void SkyboxDraw(Camera3D cam);

#endif // SKYBOX_H
