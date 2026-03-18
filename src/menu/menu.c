#include <stdio.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include "menu.h"
#include "../version.h"

// ─────────────────────────────────────────────────────────────
// Settings global
// ─────────────────────────────────────────────────────────────

Settings gSettings = {
    .sensitivity = 1.0f,
    .volume      = 0.8f,
    .resWidth    = 1280,
    .resHeight   = 720,
};

void SettingsLoad(void) {
    FILE *f = fopen("settings.cfg", "r");
    if (!f) return;
    fscanf(f, "sensitivity=%f\n", &gSettings.sensitivity);
    fscanf(f, "volume=%f\n",      &gSettings.volume);
    fscanf(f, "resWidth=%d\n",    &gSettings.resWidth);
    fscanf(f, "resHeight=%d\n",   &gSettings.resHeight);
    fclose(f);
}

void SettingsSave(void) {
    FILE *f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "sensitivity=%f\n", gSettings.sensitivity);
    fprintf(f, "volume=%f\n",      gSettings.volume);
    fprintf(f, "resWidth=%d\n",    gSettings.resWidth);
    fprintf(f, "resHeight=%d\n",   gSettings.resHeight);
    fclose(f);
}

// ─────────────────────────────────────────────────────────────
// Aesthetic constants
// ─────────────────────────────────────────────────────────────

#define COL_BG       (Color){ 8,   8,   10,  255 }
#define COL_PANEL    (Color){ 14,  14,  18,  255 }
#define COL_BORDER   (Color){ 38,  38,  50,  255 }
#define COL_ACCENT   (Color){ 220, 60,  40,  255 }   // red-orange
#define COL_ACCENT2  (Color){ 255, 120, 60,  255 }   // lighter accent
#define COL_TEXT     (Color){ 210, 210, 215, 255 }
#define COL_MUTED    (Color){ 90,  90,  100, 255 }
#define COL_INPUT_BG (Color){ 20,  20,  26,  255 }
#define COL_WHITE    (Color){ 255, 255, 255, 255 }

// ─────────────────────────────────────────────────────────────
// UI helpers
// ─────────────────────────────────────────────────────────────

static int W(void) { return GetScreenWidth();  }
static int H(void) { return GetScreenHeight(); }

// Draw a scanline overlay for atmosphere
static void DrawScanlines(void) {
    for (int y = 0; y < H(); y += 3) {
        DrawRectangle(0, y, W(), 1, (Color){ 0, 0, 0, 18 });
    }
}

// Draw a subtle grid pattern in the background
static void DrawMenuGrid(void) {
    for (int x = 0; x < W(); x += 40)
        DrawLine(x, 0, x, H(), (Color){ 255, 255, 255, 5 });
    for (int y = 0; y < H(); y += 40)
        DrawLine(0, y, W(), y, (Color){ 255, 255, 255, 5 });
}

// Animated accent line at the top
static void DrawTopAccent(float t) {
    float pulse = 0.6f + 0.4f * sinf(t * 2.0f);
    Color c = { (unsigned char)(COL_ACCENT.r * pulse),
                (unsigned char)(COL_ACCENT.g * pulse),
                (unsigned char)(COL_ACCENT.b * pulse), 255 };
    DrawRectangle(0, 0, W(), 2, c);
    DrawRectangle(0, 2, (int)(W() * 0.4f * pulse), 1, COL_ACCENT2);
}

// Panel with border
static void DrawPanel(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, COL_PANEL);
    DrawRectangleLines(x, y, w, h, COL_BORDER);
    // Accent corner marks
    int m = 6;
    DrawRectangle(x,       y,       m, 1, COL_ACCENT);
    DrawRectangle(x,       y,       1, m, COL_ACCENT);
    DrawRectangle(x+w-m,   y,       m, 1, COL_ACCENT);
    DrawRectangle(x+w-1,   y,       1, m, COL_ACCENT);
    DrawRectangle(x,       y+h-1,   m, 1, COL_ACCENT);
    DrawRectangle(x,       y+h-m,   1, m, COL_ACCENT);
    DrawRectangle(x+w-m,   y+h-1,   m, 1, COL_ACCENT);
    DrawRectangle(x+w-1,   y+h-m,   1, m, COL_ACCENT);
}

// Returns true if clicked
static bool Button(int x, int y, int w, int h, const char *label, bool hot) {
    Rectangle r = { x, y, w, h };
    bool hover  = CheckCollisionPointRec(GetMousePosition(), r);
    bool click  = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    Color bg  = hot   ? COL_ACCENT  :
                hover ? (Color){ 35, 20, 18, 255 } : COL_PANEL;
    Color bdr = hover ? COL_ACCENT2 : COL_BORDER;
    Color txt = hot   ? COL_WHITE   :
                hover ? COL_ACCENT2 : COL_TEXT;

    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, bdr);

    int fs = 20;
    int tw = MeasureText(label, fs);
    DrawText(label, x + w/2 - tw/2, y + h/2 - fs/2, fs, txt);

    if (hover) {
        // Left edge accent bar
        DrawRectangle(x, y+2, 2, h-4, COL_ACCENT);
    }
    return click;
}

// Text input box — returns true when Enter pressed
static bool TextBox(int x, int y, int w, int h, char *buf, int maxLen,
                    bool active, const char *placeholder) {
    Rectangle r = { x, y, w, h };
    bool hover  = CheckCollisionPointRec(GetMousePosition(), r);

    Color bg  = active ? COL_INPUT_BG : COL_PANEL;
    Color bdr = active ? COL_ACCENT   : hover ? COL_MUTED : COL_BORDER;

    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, bdr);

    int fs = 18;
    int len = strlen(buf);

    if (active) {
        // Handle typing
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (ch >= 32 && len < maxLen - 1) {
                buf[len++] = (char)ch;
                buf[len]   = '\0';
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
            buf[--len] = '\0';
        }
    }

    const char *display = (len == 0 && !active) ? placeholder : buf;
    Color       txtCol  = (len == 0 && !active) ? COL_MUTED : COL_TEXT;
    DrawText(display, x + 10, y + h/2 - fs/2, fs, txtCol);

    // Blinking cursor when active
    if (active && ((int)(GetTime() * 2) % 2 == 0)) {
        int cx = x + 10 + MeasureText(buf, fs);
        DrawRectangle(cx, y + 8, 2, h - 16, COL_ACCENT);
    }

    return active && IsKeyPressed(KEY_ENTER);
}

// Slider — returns new value
static float Slider(int x, int y, int w, int h, float val, float vmin, float vmax,
                    const char *label) {
    Rectangle track = { x, y + h/2 - 2, w, 4 };
    bool hover = CheckCollisionPointRec(GetMousePosition(),
                                        (Rectangle){ x, y, w, h });

    // Drag
    if (hover && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        float t = (GetMousePosition().x - x) / (float)w;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        val = vmin + t * (vmax - vmin);
    }

    float t   = (val - vmin) / (vmax - vmin);
    int   knx = x + (int)(t * w);

    // Track
    DrawRectangleRec(track, COL_BORDER);
    DrawRectangle(x, y + h/2 - 2, (int)(t * w), 4, COL_ACCENT);
    // Knob
    DrawRectangle(knx - 5, y + h/2 - 8, 10, 16,
                  hover ? COL_ACCENT2 : COL_ACCENT);
    DrawRectangleLines(knx - 5, y + h/2 - 8, 10, 16, COL_WHITE);

    // Label + value
    char valStr[32];
    snprintf(valStr, sizeof(valStr), "%s  %.2f", label, val);
    DrawText(valStr, x, y, 16, COL_MUTED);

    return val;
}

// Resolution option picker — cycles through presets
static void ResPicker(int x, int y, int w, int h) {
    static const int presets[][2] = {
        { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }
    };
    int nPresets = 4;

    // Find current index
    int idx = 0;
    for (int i = 0; i < nPresets; i++) {
        if (presets[i][0] == gSettings.resWidth &&
            presets[i][1] == gSettings.resHeight) {
            idx = i; break;
        }
    }

    int bw = 28;
    // Left arrow
    if (Button(x, y, bw, h, "<", false)) {
        idx = (idx + nPresets - 1) % nPresets;
        gSettings.resWidth  = presets[idx][0];
        gSettings.resHeight = presets[idx][1];
    }
    // Right arrow
    if (Button(x + w - bw, y, bw, h, ">", false)) {
        idx = (idx + 1) % nPresets;
        gSettings.resWidth  = presets[idx][0];
        gSettings.resHeight = presets[idx][1];
    }
    // Display
    char res[32];
    snprintf(res, sizeof(res), "%d x %d", gSettings.resWidth, gSettings.resHeight);
    int tw = MeasureText(res, 18);
    DrawRectangle(x + bw, y, w - bw*2, h, COL_INPUT_BG);
    DrawRectangleLines(x + bw, y, w - bw*2, h, COL_BORDER);
    DrawText(res, x + w/2 - tw/2, y + h/2 - 9, 18, COL_TEXT);
}

// ─────────────────────────────────────────────────────────────
// Screens
// ─────────────────────────────────────────────────────────────

typedef enum { SCR_MAIN, SCR_HOST, SCR_JOIN, SCR_SETTINGS } Screen;

static Screen   sCurrent    = SCR_MAIN;
static float    sTime       = 0.0f;

// Per-screen input state
static char  sMapFile   [128] = "assets/world/brush_test2.wrld";
static char  sServerIP  [64]  = "127.0.0.1";
static char  sPlayerName[16]  = "Player";
static int   sActiveBox = 0;   // which text box has focus

// ─────────────────────────────────────────────────────────────
// Map file list — scanned from assets/world/*.wrld on first open
// ─────────────────────────────────────────────────────────────

#define MAX_MAPS 32
static char  sMapNames [MAX_MAPS][128];  // full paths
static char  sMapLabels[MAX_MAPS][64];   // display names (filename without path/ext)
static int   sMapCount  = 0;
static int   sMapSel    = 0;     // currently selected index
static bool  sMapLoaded = false; // whether we've scanned yet
static bool  sDropOpen  = false; // dropdown open state

static void ScanMaps(void) {
    sMapCount  = 0;
    sMapLoaded = true;

    FilePathList files = LoadDirectoryFilesEx("assets/world", ".wrld", false);
    for (unsigned int i = 0; i < files.count && sMapCount < MAX_MAPS; i++) {
        strncpy(sMapNames[sMapCount], files.paths[i], 127);
        // Extract just the filename without extension for the label
        const char *base = GetFileName(files.paths[i]);
        strncpy(sMapLabels[sMapCount], base, 63);
        // Strip .wrld extension from label
        char *dot = strrchr(sMapLabels[sMapCount], '.');
        if (dot) *dot = '\0';
        sMapCount++;
    }
    UnloadDirectoryFiles(files);

    // Default selection: try to match current sMapFile
    sMapSel = 0;
    for (int i = 0; i < sMapCount; i++) {
        if (strcmp(sMapNames[i], sMapFile) == 0) { sMapSel = i; break; }
    }
    if (sMapCount > 0)
        strncpy(sMapFile, sMapNames[sMapSel], sizeof(sMapFile) - 1);
}

// Returns true if a new map was selected this frame.
// x,y,w,h = bounding box of the closed dropdown button.
// The open list renders below it; caller must size the panel accordingly.
static bool Dropdown(int x, int y, int w, int h) {
    bool changed = false;

    // Main button showing current selection
    Rectangle btnR = { x, y, w, h };
    bool hover = CheckCollisionPointRec(GetMousePosition(), btnR);
    Color bg  = sDropOpen ? COL_ACCENT : (hover ? (Color){35,20,18,255} : COL_PANEL);
    Color bdr = sDropOpen ? COL_ACCENT2 : (hover ? COL_ACCENT2 : COL_BORDER);
    DrawRectangleRec(btnR, bg);
    DrawRectangleLinesEx(btnR, 1, bdr);

    const char *label = sMapCount > 0 ? sMapLabels[sMapSel] : "(no maps found)";
    int tw = MeasureText(label, 18);
    DrawText(label, x + 10, y + h/2 - 9, 18, COL_TEXT);
    // Arrow indicator
    const char *arrow = sDropOpen ? "▲" : "▼";
    int aw = MeasureText(arrow, 16);
    DrawText(arrow, x + w - aw - 10, y + h/2 - 8, 16, COL_MUTED);
    (void)tw;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hover)
        sDropOpen = !sDropOpen;

    // Draw open list
    if (sDropOpen) {
        int itemH = 32;
        int listH = sMapCount * itemH;
        // Draw over everything else — render on top
        DrawRectangle(x, y + h, w, listH, COL_PANEL);
        DrawRectangleLines(x, y + h, w, listH, COL_ACCENT);

        for (int i = 0; i < sMapCount; i++) {
            Rectangle itemR = { x, y + h + i * itemH, w, itemH };
            bool ih = CheckCollisionPointRec(GetMousePosition(), itemR);
            bool sel = (i == sMapSel);
            DrawRectangleRec(itemR, sel ? (Color){40,20,16,255} : (ih ? (Color){25,18,15,255} : COL_PANEL));
            if (sel) DrawRectangle(x, (int)itemR.y + 2, 3, itemH - 4, COL_ACCENT);
            DrawText(sMapLabels[i], x + 12, (int)itemR.y + itemH/2 - 9, 18,
                     sel ? COL_ACCENT2 : (ih ? COL_TEXT : COL_MUTED));
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && ih) {
                sMapSel = i;
                strncpy(sMapFile, sMapNames[i], sizeof(sMapFile) - 1);
                sDropOpen = false;
                changed   = true;
            }
        }
        // Close if clicked outside
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            !CheckCollisionPointRec(GetMousePosition(),
                (Rectangle){ x, y, w, h + listH })) {
            sDropOpen = false;
        }
    }

    return changed;
}

// ─────────────────────────────────────────────────────────────
// Title treatment
// ─────────────────────────────────────────────────────────────

static void DrawTitle(void) {
    const char *title = "VITAL";
    int fs  = 72;
    int tw  = MeasureText(title, fs);
    int tx  = W()/2 - tw/2;
    int ty  = 48;

    // Shadow layers for depth
    DrawText(title, tx+3, ty+3, fs, (Color){ COL_ACCENT.r/3,
                                              COL_ACCENT.g/3,
                                              COL_ACCENT.b/3, 200 });
    DrawText(title, tx+1, ty+1, fs, COL_ACCENT);
    DrawText(title, tx,   ty,   fs, COL_WHITE);

    // Subtitle
    const char *sub = "ARENA  //  DEATHMATCH";
    int sw = MeasureText(sub, 14);
    DrawText(sub, W()/2 - sw/2, ty + fs + 8, 14, COL_MUTED);

    // Decorative line under title
    DrawRectangle(W()/2 - 120, ty + fs + 28, 240, 1, COL_BORDER);
    DrawRectangle(W()/2 - 40,  ty + fs + 28, 80,  1, COL_ACCENT);
}

// ─────────────────────────────────────────────────────────────
// Main screen
// ─────────────────────────────────────────────────────────────

static MenuResult DrawMainScreen(void) {
    int pw = 280, ph = 220;
    int px = W()/2 - pw/2, py = H()/2 - 20;
    DrawPanel(px, py, pw, ph);

    int bw = pw - 40, bh = 44;
    int bx = px + 20;
    int gap = 12;
    int by = py + 20;

    if (Button(bx, by,          bw, bh, "HOST GAME",   false)) { sCurrent = SCR_HOST; sActiveBox = 0; sMapLoaded = false; sDropOpen = false; }
    if (Button(bx, by+bh+gap,   bw, bh, "JOIN GAME",   false)) { sCurrent = SCR_JOIN;     sActiveBox = 0; }
    if (Button(bx, by+bh*2+gap*2, bw, bh, "SETTINGS",  false)) { sCurrent = SCR_SETTINGS; }
    if (Button(bx, by+bh*3+gap*3, bw, bh, "QUIT",      false)) return MENU_QUIT;

    return MENU_NONE;
}

// ─────────────────────────────────────────────────────────────
// Host screen
// ─────────────────────────────────────────────────────────────

static MenuResult DrawHostScreen(MenuChoice *out) {
    if (!sMapLoaded) ScanMaps();

    // Panel is taller when dropdown is open to accommodate the list
    int itemH   = 32;
    int listH   = sDropOpen ? sMapCount * itemH : 0;
    int pw = 400, ph = 230 + listH;
    int px = W()/2 - pw/2, py = H()/2 - ph/2;
    DrawPanel(px, py, pw, ph);

    int lx = px + 20, rx = px + 20;
    int iy = py + 20, iw = pw - 40, ih = 34;

    DrawText("MAP", lx, iy - 16, 13, COL_MUTED);
    Dropdown(rx, iy, iw, ih);

    iy += ih + listH + 32;
    DrawText("PLAYER NAME", lx, iy - 16, 13, COL_MUTED);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        !sDropOpen &&
        CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){ rx, iy, iw, ih })) sActiveBox = 2;
    TextBox(rx, iy, iw, ih, sPlayerName, sizeof(sPlayerName), sActiveBox == 2, "Player");

    iy += ih + 20;
    int bw = (iw - 10) / 2;
    if (!sDropOpen && Button(rx, iy, bw, 38, "START", true)) {
        strncpy(out->mapFile,    sMapFile,    sizeof(out->mapFile) - 1);
        strncpy(out->playerName, sPlayerName, sizeof(out->playerName) - 1);
        return MENU_HOST;
    }
    if (!sDropOpen && Button(rx+bw+10, iy, bw, 38, "BACK", false)) {
        sCurrent   = SCR_MAIN;
        sDropOpen  = false;
    }

    return MENU_NONE;
}

// ─────────────────────────────────────────────────────────────
// Join screen
// ─────────────────────────────────────────────────────────────

static MenuResult DrawJoinScreen(MenuChoice *out) {
    int pw = 400, ph = 230;
    int px = W()/2 - pw/2, py = H()/2 - ph/2;
    DrawPanel(px, py, pw, ph);

    int lx = px + 20, rx = px + 20;
    int iy = py + 20, iw = pw - 40, ih = 34;

    DrawText("SERVER IP", lx, iy - 16, 13, COL_MUTED);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){ rx, iy, iw, ih })) sActiveBox = 1;
    TextBox(rx, iy, iw, ih, sServerIP, sizeof(sServerIP), sActiveBox == 1, "127.0.0.1");

    iy += ih + 32;
    DrawText("PLAYER NAME", lx, iy - 16, 13, COL_MUTED);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){ rx, iy, iw, ih })) sActiveBox = 2;
    TextBox(rx, iy, iw, ih, sPlayerName, sizeof(sPlayerName), sActiveBox == 2, "Player");

    iy += ih + 20;
    int bw = (iw - 10) / 2;
    if (Button(rx,       iy, bw, 38, "CONNECT", true)) {
        strncpy(out->serverIP,   sServerIP,   sizeof(out->serverIP) - 1);
        strncpy(out->playerName, sPlayerName, sizeof(out->playerName) - 1);
        return MENU_JOIN;
    }
    if (Button(rx+bw+10, iy, bw, 38, "BACK", false)) {
        sCurrent = SCR_MAIN;
    }

    return MENU_NONE;
}

// ─────────────────────────────────────────────────────────────
// Settings screen
// ─────────────────────────────────────────────────────────────

static void DrawSettingsScreen(void) {
    int pw = 420, ph = 280;
    int px = W()/2 - pw/2, py = H()/2 - ph/2;
    DrawPanel(px, py, pw, ph);

    int lx = px + 24;
    int sw = pw - 48;
    int iy = py + 24;
    int sh = 40;  // slider row height
    int gap = 18;

    DrawText("SETTINGS", lx, iy, 20, COL_ACCENT);
    DrawRectangle(lx, iy + 24, sw, 1, COL_BORDER);
    iy += 36;

    DrawText("MOUSE SENSITIVITY", lx, iy, 13, COL_MUTED);
    iy += 18;
    gSettings.sensitivity = Slider(lx, iy, sw, sh, gSettings.sensitivity, 0.1f, 5.0f, "");
    iy += sh + gap;

    DrawText("MASTER VOLUME", lx, iy, 13, COL_MUTED);
    iy += 18;
    gSettings.volume = Slider(lx, iy, sw, sh, gSettings.volume, 0.0f, 1.0f, "");
    iy += sh + gap;

    DrawText("RESOLUTION", lx, iy, 13, COL_MUTED);
    iy += 18;
    ResPicker(lx, iy, sw, 34);
    iy += 34 + gap;

    if (Button(lx, iy, sw, 38, "BACK", false)) {
        SettingsSave();
        sCurrent = SCR_MAIN;
    }
}

// ─────────────────────────────────────────────────────────────
// RunMenu
// ─────────────────────────────────────────────────────────────

MenuResult RunMenu(MenuChoice *out) {
    SettingsLoad();
    memset(out, 0, sizeof(*out));
    strncpy(out->mapFile, "assets/world/brush_test2.wrld", sizeof(out->mapFile) - 1);

    // Temporary window for the menu (will be resized to chosen resolution after)
    InitWindow(1280, 720, "VITAL");
    SetTargetFPS(165);
    // Show cursor in menu
    ShowCursor();

    MenuResult result = MENU_NONE;

    while (!WindowShouldClose() && result == MENU_NONE) {
        sTime += GetFrameTime();

        BeginDrawing();
        ClearBackground(COL_BG);

        DrawMenuGrid();
        DrawTopAccent(sTime);
        DrawTitle();
        DrawScanlines();

        // Screen label top-right
        const char *scrName = sCurrent == SCR_MAIN     ? "MAIN MENU" :
                              sCurrent == SCR_HOST     ? "HOST GAME" :
                              sCurrent == SCR_JOIN     ? "JOIN GAME" : "SETTINGS";
        int snw = MeasureText(scrName, 13);
        DrawText(scrName, W() - snw - 16, 12, 13, COL_MUTED);

        // Version bottom-left
        DrawText("BUILD " VERSION, 12, H() - 22, 12, COL_MUTED);

        switch (sCurrent) {
        case SCR_MAIN:     result = DrawMainScreen();       break;
        case SCR_HOST:     result = DrawHostScreen(out);    break;
        case SCR_JOIN:     result = DrawJoinScreen(out);    break;
        case SCR_SETTINGS: DrawSettingsScreen();            break;
        }

        EndDrawing();
    }

    if (WindowShouldClose() && result == MENU_NONE)
        result = MENU_QUIT;

    CloseWindow();
    return result;
}
