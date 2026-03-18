#ifndef MENU_H
#define MENU_H

#include <stdbool.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// Settings — persisted to disk, read by the rest of the game
// ─────────────────────────────────────────────────────────────

typedef struct {
    float  sensitivity;     // mouse sensitivity multiplier (default 1.0)
    float  volume;          // master volume 0..1
    int    resWidth;
    int    resHeight;
} Settings;

extern Settings gSettings;

// ─────────────────────────────────────────────────────────────
// Menu result — what the player chose
// ─────────────────────────────────────────────────────────────

typedef enum {
    MENU_NONE    = 0,   // still running
    MENU_HOST    = 1,   // player wants to host
    MENU_JOIN    = 2,   // player wants to join
    MENU_QUIT    = 3,
} MenuResult;

// Filled by RunMenu() when it returns MENU_HOST or MENU_JOIN
typedef struct {
    char mapFile   [128];
    char serverIP  [64];
    char playerName[16];
} MenuChoice;

// Run the blocking menu loop. Returns when the player commits to an action.
// Call before InitWindow so we can apply the chosen resolution.
MenuResult RunMenu(MenuChoice *out);

// Load / save settings from "settings.cfg" next to the executable.
void SettingsLoad(void);
void SettingsSave(void);

#endif // MENU_H
