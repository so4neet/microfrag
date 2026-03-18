#include <string.h>
#include <raylib.h>
#include "audio.h"
#include "../menu/menu.h"
#include "../game/weapon.h"

// ─────────────────────────────────────────────────────────────
// Internal state
// ─────────────────────────────────────────────────────────────

static Music sMusic       = { 0 };
static bool  sMusicLoaded = false;

// Sound pool — multiple instances per sound so rapid fire doesn't cut off.
#define POOL_SIZE 8

typedef struct {
    Sound slots[POOL_SIZE];
    int   next;   // round-robin index
    bool  loaded;
} SoundPool;

static SoundPool sShootPools[WEAPON_COUNT];
static SoundPool sHitPool;
static SoundPool sDeathPool;
static SoundPool sSpawnPool;

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

static bool TryLoadPool(SoundPool *pool, const char *path) {
    if (!FileExists(path)) {
        TraceLog(LOG_WARNING, "AUDIO: sound not found: %s", path);
        return false;
    }
    for (int i = 0; i < POOL_SIZE; i++)
        pool->slots[i] = LoadSound(path);
    pool->next   = 0;
    pool->loaded = true;
    TraceLog(LOG_INFO, "AUDIO: loaded pool %s (%d slots)", path, POOL_SIZE);
    return true;
}

static void UnloadPool(SoundPool *pool) {
    if (!pool->loaded) return;
    for (int i = 0; i < POOL_SIZE; i++)
        UnloadSound(pool->slots[i]);
    pool->loaded = false;
}

static void PlayPool(SoundPool *pool, float volume) {
    if (!pool->loaded) return;
    Sound *s = &pool->slots[pool->next];
    pool->next = (pool->next + 1) % POOL_SIZE;
    SetSoundVolume(*s, volume);
    PlaySound(*s);
}

static Music TryLoadMusic(const char *path) {
    if (!FileExists(path)) {
        TraceLog(LOG_WARNING, "AUDIO: music not found: %s", path);
        return (Music){ 0 };
    }
    Music m = LoadMusicStream(path);
    TraceLog(LOG_INFO, "AUDIO: loaded music %s", path);
    return m;
}

// ─────────────────────────────────────────────────────────────
// Per-weapon shoot sound paths
// Add a new entry here when adding a weapon to WeaponDef.
// ─────────────────────────────────────────────────────────────

static const char *sShootPaths[WEAPON_COUNT] = {
    [WEAPON_SHOTGUN] = "assets/audio/shoot_shotgun.wav",
    [WEAPON_ROCKET]  = "assets/audio/shoot_rocket.wav",
};

// ─────────────────────────────────────────────────────────────
// AudioInit / Shutdown
// ─────────────────────────────────────────────────────────────

void AudioInit(void) {
    // Music
    sMusic = TryLoadMusic("assets/audio/music.ogg");
    if (sMusic.stream.buffer != NULL) {
        sMusicLoaded = true;
        SetMusicVolume(sMusic, gSettings.volume * 0.4f); // music quieter than SFX
        PlayMusicStream(sMusic);
    }

    // Weapon fire sound pools
    for (int i = 0; i < WEAPON_COUNT; i++)
        if (sShootPaths[i])
            TryLoadPool(&sShootPools[i], sShootPaths[i]);

    // One-shot sound pools
    TryLoadPool(&sHitPool,   "assets/audio/hit.wav");
    TryLoadPool(&sDeathPool, "assets/audio/death.wav");
    TryLoadPool(&sSpawnPool, "assets/audio/spawn.wav");
}

void AudioShutdown(void) {
    if (sMusicLoaded)  { StopMusicStream(sMusic); UnloadMusicStream(sMusic); }
    for (int i = 0; i < WEAPON_COUNT; i++)
        UnloadPool(&sShootPools[i]);
    UnloadPool(&sHitPool);
    UnloadPool(&sDeathPool);
    UnloadPool(&sSpawnPool);
}

// ─────────────────────────────────────────────────────────────
// AudioUpdate — call every frame
// ─────────────────────────────────────────────────────────────

void AudioUpdate(void) {
    if (sMusicLoaded) {
        // Keep music volume in sync with settings (user may change it at runtime)
        SetMusicVolume(sMusic, gSettings.volume * 0.4f);
        UpdateMusicStream(sMusic);
    }
}

// ─────────────────────────────────────────────────────────────
// Trigger functions
// ─────────────────────────────────────────────────────────────

void AudioPlayShoot(uint8_t weaponId) {
    if (weaponId >= WEAPON_COUNT) return;
    PlayPool(&sShootPools[weaponId], gSettings.volume);
}

void AudioPlayHit(void) {
    PlayPool(&sHitPool, gSettings.volume);
}

void AudioPlayDeath(bool isLocal) {
    PlayPool(&sDeathPool, gSettings.volume * (isLocal ? 1.0f : 0.6f));
}

void AudioPlaySpawn(bool isLocal) {
    PlayPool(&sSpawnPool, gSettings.volume * (isLocal ? 1.0f : 0.5f));
}
