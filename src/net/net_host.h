#ifndef NET_HOST_H
#define NET_HOST_H

#include <stdbool.h>
#include "../net/net.h"

// Call once before the game loop. Binds the ENet host on NET_PORT.
// mapName: path to the level file (sent to connecting clients).
// Returns true on success.
bool HostInit(const char *mapName);

// Drain all pending ENet events (connections, disconnections, packets).
// Call once per frame before HostTick().
void HostPollNetwork(void);

// Run authoritative simulation for all remote players (slots 1-N).
// The host's own player (slot 0) is driven by player.c, not this.
void HostTick(float dt);

// Broadcast a SnapshotMsg to every connected client.
// Rate-limited internally to SNAPSHOT_HZ — safe to call every frame.
void HostBroadcastSnapshot(void);

// Send a reliable EventMsg to all connected clients.
void HostBroadcastEvent(uint8_t eventType, uint8_t instigator,
                        uint8_t target, int16_t value);

void HostShutdown(void);

#endif // NET_HOST_H
