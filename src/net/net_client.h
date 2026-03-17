#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stdbool.h>
#include "../net/net.h"

// Connect to a listen server. playerName shown to other players.
// Returns true if the connection handshake succeeds (blocks briefly).
bool ClientConnect(const char *host, uint16_t port, const char *playerName);

// Drain ENet events — call once per frame.
// Populates gameState with the latest snapshot from the host.
void ClientPollNetwork(void);

// Build an InputMsg from the current local input state and send it.
// Also runs client-side prediction for the local player.
void ClientSendInput(float forwardAxis, float sideAxis,
                     float yaw, float pitch, uint8_t buttons, float dt);

// True once MSG_JOIN_ACK has been received and the level is loaded.
bool ClientIsConnected(void);

void ClientDisconnect(void);

// Advance interpolated remote player positions. Call once per frame before rendering.
void ClientInterpolateRemote(void);

#endif // NET_CLIENT_H
