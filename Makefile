# ─────────────────────────────────────────────────────────────
# Vital — Makefile
# ─────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 \
          -I src \
          -I src/net \
          -I src/game \
          -I src/player \
          -I src/world

# Libraries — adjust paths for your system if needed
LIBS    = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -lenet

# Source files
SRC = src/main.c \
      src/player/player.c \
      src/world/worldobject.c \
      src/game/game_logic.c \
      src/net/net_host.c \
      src/net/net_client.c

OBJ = $(SRC:.c=.o)

TARGET = vital

# ─────────────────────────────────────────────────────────────

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
