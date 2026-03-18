#ifndef GLOBALS_H
#define GLOBALS_H

#include <raylib.h>

#define APPENDPATH(path) path

// ─────────────────────────────────────────────────────────────
// Quake-style movement tuning
// ─────────────────────────────────────────────────────────────

// Top speed on the ground (units/sec)
#define moveSpeed           6.0f

// Ground acceleration — how fast you reach moveSpeed when grounded
// Quake used 10, higher = snappier
#define groundAccel         10.0f

// Air acceleration — kept low so air-strafing is skillful, not free
// Quake used 1, CPM/CPMA raised this for trickier movement
#define airAccel            4.0f

// Ground friction — how fast you decelerate when no input is held
// Quake used 4, lower = icier
#define friction            6.0f

// Maximum horizontal speed bhop can build up to (0 = no cap)
#define maxBhopSpeed        30.0f
#define lookSpeed           0.05f
#define gravity             6.6f
#define jumpForce           2.6f
#define playerHeight        1.5f
#define playerRadius        0.2f
#define maxStepHeight       0.05

// Brush mesh subdivision — one grid cell per this many world units.
// Must be identical in editor (world_io.c) and game (worldobject.c).
#define BRUSH_SUBDIV_SIZE  3.0f
#define BRUSH_MIN_STEPS    1

// ─────────────────────────────────────────────────────────────
// Lightmap atlas — shared between editor and game
// ─────────────────────────────────────────────────────────────

#define LIGHTMAP_SIZE             512
#define LIGHTMAP_LUXELS_PER_UNIT  4.0f

typedef struct {
    int   atlasX, atlasY;
    int   width,  height;
    float u0, v0;
    float u1, v1;
} LightmapFaceRegion;

typedef struct {
    LightmapFaceRegion faces[6];
} LightmapBrushEntry;

// Game-side only needs the GPU texture + UV layout.
// pixels pointer is NULL at runtime (never loaded into RAM).
typedef struct {
    unsigned char      *pixels;       // NULL in game
    int                 brushCount;
    LightmapBrushEntry *brushes;      // heap allocated
    Texture2D           atlasTexture; // GPU texture, loaded at level load
} LightmapAtlas;

#endif // GLOBALS_H
