# ─────────────────────────────────────────────────────────────
# Vital — Makefile
# Builds raylib and ENet from vendored source in deps/
# No system library installs required.
# ─────────────────────────────────────────────────────────────

CC     = gcc
AR     = ar
CFLAGS = -Wall -Wextra -O2 -std=c11

# ─────────────────────────────────────────────────────────────
# Paths
# ─────────────────────────────────────────────────────────────

RAYLIB_DIR  = deps/raylib/src
ENET_DIR    = deps/enet

RAYLIB_LIB  = $(RAYLIB_DIR)/libraylib.a
ENET_LIB    = $(ENET_DIR)/libenet.a

# ─────────────────────────────────────────────────────────────
# Include paths for the game
# ─────────────────────────────────────────────────────────────

INCLUDES = \
    -I src \
    -I src/net \
    -I src/game \
    -I src/player \
    -I src/world \
    -I src/menu \
    -I src/audio \
    -I src/skybox \
    -I $(RAYLIB_DIR) \
    -I $(ENET_DIR)/include

# ─────────────────────────────────────────────────────────────
# Game source files
# ─────────────────────────────────────────────────────────────

SRC = \
    src/main.c \
    src/menu/menu.c \
    src/audio/audio.c \
    src/skybox/skybox.c \
    src/player/player.c \
    src/world/worldobject.c \
    src/game/game_logic.c \
    src/game/weapon.c \
    src/game/tracer.c \
    src/net/net_host.c \
    src/net/net_client.c

OBJ = $(SRC:.c=.o)

TARGET = vital

# System libs
SYS_LIBS = -lGL -lm -lpthread -ldl -lrt -lX11

# ─────────────────────────────────────────────────────────────
# Top-level targets
# ─────────────────────────────────────────────────────────────

all: $(RAYLIB_LIB) $(ENET_LIB) $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(RAYLIB_LIB) $(ENET_LIB) $(SYS_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ─────────────────────────────────────────────────────────────
# Build raylib as a static library
# ─────────────────────────────────────────────────────────────

$(RAYLIB_LIB):
	$(MAKE) -C $(RAYLIB_DIR) PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC

# ─────────────────────────────────────────────────────────────
# Build ENet as a static library from source.
# ─────────────────────────────────────────────────────────────

ENET_SRC = \
    $(ENET_DIR)/callbacks.c \
    $(ENET_DIR)/compress.c \
    $(ENET_DIR)/host.c \
    $(ENET_DIR)/list.c \
    $(ENET_DIR)/packet.c \
    $(ENET_DIR)/peer.c \
    $(ENET_DIR)/protocol.c \
    $(ENET_DIR)/unix.c

ENET_OBJ = $(ENET_SRC:.c=.o)

$(ENET_DIR)/%.o: $(ENET_DIR)/%.c
	$(CC) -O2 -I $(ENET_DIR)/include -c -o $@ $<

$(ENET_LIB): $(ENET_OBJ)
	$(AR) rcs $@ $^

# ─────────────────────────────────────────────────────────────

clean:
	rm -f $(OBJ) $(TARGET)

clean-deps:
	$(MAKE) -C $(RAYLIB_DIR) clean
	rm -f $(ENET_OBJ) $(ENET_LIB)

clean-all: clean clean-deps

.PHONY: all clean clean-deps clean-all
