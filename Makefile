# TvStreamer Launcher (C Edition)
# Makefile for building the ultra-lightweight SDL2 launcher

CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra -DNDEBUG
CFLAGS_DEBUG = -g -O0 -Wall -Wextra -DDEBUG

# SDL2 flags
SDL_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
SDL_LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)

# Additional libraries
LIBS = -lm -lpthread

# Source files
SRC = launcher.c
OBJ = $(SRC:.c=.o)

# Output
TARGET = tvstreamer-launcher

# Raspberry Pi optimized flags
PI_CFLAGS = -O3 -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard -Wall -Wextra -DNDEBUG

.PHONY: all clean debug install pi

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) $(LIBS)

debug: $(SRC)
	$(CC) $(CFLAGS_DEBUG) $(SDL_CFLAGS) -o $(TARGET)-debug $< $(SDL_LIBS) $(LIBS)

# Build for Raspberry Pi 4 (aarch64)
pi: $(SRC)
	$(CC) $(PI_CFLAGS) $(SDL_CFLAGS) -o $(TARGET) $< $(SDL_LIBS) $(LIBS)

# Install to ~/.local/bin
install: $(TARGET)
	install -m 755 $(TARGET) ~/.local/bin/

clean:
	rm -f $(TARGET) $(TARGET)-debug $(OBJ)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists sdl2 && echo "SDL2: OK" || echo "SDL2: MISSING"
	@pkg-config --exists SDL2_ttf && echo "SDL2_ttf: OK" || echo "SDL2_ttf: MISSING"
	@pkg-config --exists SDL2_image && echo "SDL2_image: OK" || echo "SDL2_image: MISSING"
	@echo "Font check:"
	@ls /usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf 2>/dev/null && echo "Nerd Font: OK" || echo "Nerd Font: MISSING (install ttf-jetbrains-mono-nerd)"
