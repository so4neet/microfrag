#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// Audio system
//
// Expected asset paths:
//   assets/audio/music.ogg          — background music loop
//   assets/audio/shoot_shotgun.wav  — shotgun fire
//   assets/audio/shoot_rocket.wav   — rocket launcher fire
//   assets/audio/hit.wav            — bullet impact on a player
//   assets/audio/death.wav          — player death
//   assets/audio/spawn.wav          — player spawn/respawn
//
// All files are optional — missing files log a warning and are skipped.
// ─────────────────────────────────────────────────────────────

// Call once after InitAudioDevice().
void AudioInit(void);

// Call once at shutdown.
void AudioShutdown(void);

// Call every frame — keeps music streaming and applies volume.
void AudioUpdate(void);

// ── Trigger functions ─────────────────────────────────────────

// Fire sound for weapon index (matches WeaponID enum).
void AudioPlayShoot(uint8_t weaponId);

// Bullet hit a player.
void AudioPlayHit(void);

// A player died. Pass true if it was the local player.
void AudioPlayDeath(bool isLocal);

// A player spawned/respawned. Pass true if it was the local player.
void AudioPlaySpawn(bool isLocal);

#endif // AUDIO_H
