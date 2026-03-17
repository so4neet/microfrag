#pragma once
#include <raylib.h>

void InitCamera(void);
void HandlePlayer(void);
void HandleGravity(void);       // stub — gravity lives in SimulatePlayer now
Camera3D GetPlayerCamera(void);
