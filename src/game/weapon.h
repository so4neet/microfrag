#ifndef WEAPON_H
#define WEAPON_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// Weapon registry
//
// To add a new weapon:
//   1. Add a WEAPON_* constant to WeaponID
//   2. Add a WeaponDef row to weapons[] in weapon.c
//      (geometry is procedural — no model files needed)
// ─────────────────────────────────────────────────────────────

typedef enum {
    WEAPON_SHOTGUN = 0,
    WEAPON_ROCKET,
    // add more here
    WEAPON_COUNT
} WeaponID;

// Shape used to procedurally build the weapon mesh
typedef enum {
    WSHAPE_BOX,       // flat rectangular body (shotgun, pistol, etc.)
    WSHAPE_TUBE,      // cylinder tube (rocket launcher, railgun, etc.)
} WeaponShape;

typedef struct {
    const char  *name;

    // Procedural geometry — no model files
    WeaponShape  shape;
    Color        bodyColor;    // main colour of the mesh
    Color        accentColor;  // barrel / secondary detail colour
    float        bodyLength;   // world units (scaled down for viewmodel)
    float        bodyWidth;
    float        bodyHeight;
    float        barrelLength; // extra tube on the front
    float        barrelRadius;

    // Fire behaviour
    float  fireRate;    // seconds between shots
    int    damage;      // per hitscan hit
    float  spread;      // cone half-angle in radians (0 = perfectly accurate)
    int    pellets;     // hitscan rays per shot
    float  range;       // max hitscan range in world units

    // Feel
    float  screenShake;
    float  recoilPitch;
} WeaponDef;

// Read-only table — index by WeaponID
extern const WeaponDef weapons[WEAPON_COUNT];

// Fire / cycle
// Tick cooldown for a player — call once per frame.
void WeaponTickCooldown(int slot, float dt);

bool WeaponTryFire(int shooterSlot, float dt);
void WeaponCycleNext(int slot);
void WeaponCyclePrev(int slot);

// Build procedural models. Call once after InitWindow.
void WeaponsInit(void);
void WeaponsShutdown(void);

// View model (first-person) and world model (shown on other players).
// Both are procedurally generated — never NULL after WeaponsInit.
Model *WeaponGetViewModel(uint8_t weaponId);
Model *WeaponGetWorldModel(uint8_t weaponId);

#endif // WEAPON_H
