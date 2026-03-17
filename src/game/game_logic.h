#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "game_state.h"
#include "../net/net.h"

// Deterministic single-player tick.
// Called by:
//   - Host: for every remote player each server tick
//   - Client: locally for prediction of the local player
// Does NOT touch the camera — caller syncs camera from PlayerState.
void SimulatePlayer(PlayerState *p, const InputMsg *input, float dt);

// Respawn a player at the world spawn point.
void RespawnPlayer(PlayerState *p);

#endif // GAME_LOGIC_H
