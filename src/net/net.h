#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <enet/enet.h>

// ─────────────────────────────────────────────────────────────
// Connection config
// ─────────────────────────────────────────────────────────────

#define NET_PORT        27960
#define MAX_CLIENTS     7       // + 1 host (slot 0) = 8 total
#define MAX_PLAYERS     (MAX_CLIENTS + 1)
#define SNAPSHOT_HZ     60
#define TICKRATE_HZ     60

// ENet channel assignments
#define CHAN_UNRELIABLE  0      // input msgs, snapshots
#define CHAN_RELIABLE    1      // events: frags, spawns, damage
#define NUM_CHANNELS     2

// Button bitmask (sent in InputMsg)
#define BTN_JUMP        (1 << 0)
#define BTN_FIRE        (1 << 1)
#define BTN_NEXT_WEP    (1 << 2)
#define BTN_PREV_WEP    (1 << 3)

// ─────────────────────────────────────────────────────────────
// Message types
// ─────────────────────────────────────────────────────────────

typedef enum {
    // Client → Host
    MSG_INPUT       = 1,

    // Host → Client
    MSG_SNAPSHOT    = 2,
    MSG_EVENT       = 3,
    MSG_HIT         = 7,    // hitscan fired — spawn tracer + impact fx

    // Handshake (reliable)
    MSG_JOIN_REQ    = 4,
    MSG_JOIN_ACK    = 5,
    MSG_DISCONNECT  = 6,
    MSG_NAMES       = 8,    // host → client: full name table
} MsgType;

// ─────────────────────────────────────────────────────────────
// Wire structs — keep packed so sizeof is predictable
// ─────────────────────────────────────────────────────────────

// Client → Host, CHAN_UNRELIABLE, every rendered frame
typedef struct {
    uint8_t  type;          // MSG_INPUT
    uint32_t sequence;      // monotonically increasing, for reconciliation
    float    forwardAxis;   // -1..1
    float    sideAxis;      // -1..1  (strafe)
    float    yaw;           // absolute radians
    float    pitch;         // absolute radians
    uint8_t  buttons;       // BTN_* bitmask
    float    dt;            // client frame time — host uses this to simulate
} __attribute__((packed)) InputMsg;

// Per-player state inside a snapshot
typedef struct {
    uint8_t  active;
    float    x, y, z;
    float    yaw;
    int16_t  health;
    uint8_t  weapon;
    // Physics state — needed for client reconciliation
    float    vx, vz;        // horizVel
    float    vy;            // vertVelocity
    uint8_t  grounded;
} __attribute__((packed)) NetPlayer;

// Host → All clients, CHAN_UNRELIABLE, at SNAPSHOT_HZ
typedef struct {
    uint8_t   type;                     // MSG_SNAPSHOT
    uint32_t  serverTick;
    uint32_t  lastInputAck;             // per-recipient: highest seq processed
    NetPlayer players[MAX_PLAYERS];     // slot 0 = host
} __attribute__((packed)) SnapshotMsg;

// Host → All clients, CHAN_RELIABLE
typedef enum {
    EVENT_FRAG   = 1,
    EVENT_SPAWN  = 2,
    EVENT_DAMAGE = 3,
} EventType;

// Sent reliably host→all when a hitscan shot is fired.
// Clients use this to draw tracers + impact fx.
typedef struct {
    uint8_t type;        // MSG_HIT
    uint8_t shooter;     // player slot
    float   ox, oy, oz;  // ray origin
    float   hx, hy, hz;  // hit point (or far-plane point if miss)
    uint8_t hit;         // 1 = hit a player, 0 = hit world/miss
} __attribute__((packed)) HitMsg;

typedef struct {
    uint8_t type;       // MSG_EVENT
    uint8_t eventType;  // EventType
    uint8_t instigator; // player slot
    uint8_t target;     // player slot
    int16_t value;      // damage amount, etc.
} __attribute__((packed)) EventMsg;

// Client → Host, CHAN_RELIABLE
typedef struct {
    uint8_t  type;          // MSG_JOIN_REQ
    char     name[16];      // player name, null-terminated
} __attribute__((packed)) JoinReqMsg;

// Host → Client, CHAN_RELIABLE
typedef struct {
    uint8_t  type;          // MSG_JOIN_ACK
    uint8_t  slot;          // assigned player slot (1-7)
    char     mapName[64];   // level file path
    char     names[MAX_PLAYERS][16];  // all current player names
} __attribute__((packed)) JoinAckMsg;

// Sent reliably to all clients when any player joins/leaves.
typedef struct {
    uint8_t  type;          // MSG_NAMES
    char     names[MAX_PLAYERS][16];
} __attribute__((packed)) NamesMsg;

#endif // NET_H
