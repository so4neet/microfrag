#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>
#include "../net/net.h"

// ─────────────────────────────────────────────────────────────
// Authoritative per-player state (lives on host, simulated by
// game_logic.c; mirrored on clients via snapshots)
// ─────────────────────────────────────────────────────────────

typedef struct {
    bool     active;
    bool     grounded;
    Vector3  pos;
    float    yaw;           // radians, horizontal look
    float    pitch;         // radians, vertical look
    float    vertVelocity;
    int      health;
    uint32_t lastInputSeq;  // highest InputMsg.sequence processed
    char     name[16];
} PlayerState;

// ─────────────────────────────────────────────────────────────
// Full game state — one instance, owned by the host process.
// Clients hold a read-only mirror updated by snapshots.
// ─────────────────────────────────────────────────────────────

typedef struct {
    PlayerState players[MAX_PLAYERS];   // slot 0 = host/listen-server player
    uint32_t    tick;
} GameState;

extern GameState gameState;

// Local player's assigned slot (0 if hosting, 1-7 if client)
extern int localSlot;

#endif // GAME_STATE_H
