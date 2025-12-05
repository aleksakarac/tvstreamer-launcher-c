# TvStreamer Launcher (C Edition)

Ultra-lightweight SDL2 media center launcher with Arc Blueberry theme.

![Version](https://img.shields.io/badge/version-1.0.0-blue)
![Language](https://img.shields.io/badge/language-C-green)
![License](https://img.shields.io/badge/license-MIT-purple)

## Performance

Compared to the Python/Pygame version:

| Metric | Python v1.1 | C v1.0 | Improvement |
|--------|-------------|--------|-------------|
| Idle CPU | ~1-5% | <0.5% | 5-10x |
| Memory | ~160MB | ~15-20MB | 8-10x |
| Startup | ~1-2s | <100ms | 10-20x |
| Binary | Interpreted | ~50KB | N/A |

## Features

- Event-driven rendering (near-zero idle CPU)
- Pre-cached textures for all UI elements
- Hardware-accelerated with VSync
- Minimal memory footprint
- Glassmorphism UI with semi-transparent tiles
- Wallpaper background support
- Nerd Font icons
- Real-time system stats (CPU, RAM, Temp, Disk)
- Keyboard navigation for remote control

## Dependencies

- SDL2
- SDL2_ttf
- SDL2_image
- JetBrains Mono Nerd Font (for icons)

### Arch Linux / Raspberry Pi OS

```bash
sudo pacman -S sdl2 sdl2_ttf sdl2_image ttf-jetbrains-mono-nerd
```

### Debian/Ubuntu

```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev fonts-jetbrains-mono
```

## Building

```bash
# Standard build (optimized)
make

# Debug build
make debug

# Raspberry Pi 4 optimized build
make pi

# Check dependencies
make check-deps

# Install to ~/.local/bin
make install
```

## Usage

```bash
./tvstreamer-launcher
```

### Keyboard Controls

| Key | Action |
|-----|--------|
| Left/Right | Navigate between apps |
| Up | Select settings icon |
| Down | Return to app tiles |
| Enter | Launch selected app |
| R | Reboot (with confirmation) |
| P | Power off (with confirmation) |
| Q / Esc | Quit launcher |

## Configuration

Edit the `apps` array in `launcher.c` to customize applications:

```c
static const App apps[NUM_APPS] = {
    {"App Name", "command", ICON_CONSTANT},
    {"Kodi", "kodi", ICON_TV},
    // ...
};
```

### Wallpaper

Place your wallpaper at `~/wallpapers/1.png`.

## Theme

Uses the **Omarchy Arc Blueberry** color palette:

| Color | Hex | Usage |
|-------|-----|-------|
| Background | `#111422` | Main background |
| Secondary | `#1a1e33` | Tiles, panels |
| Foreground | `#bcc1dc` | Primary text |
| Accent | `#8eb0e6` | Selection |
| Pink | `#f38cec` | Selected borders |
| Green | `#3CEC85` | Good stats |
| Yellow | `#EACD61` | Warning stats |
| Red | `#E35535` | Critical stats |

## Architecture

```
┌─────────────────────────────────────────┐
│           Main Thread                    │
│  ┌─────────────────────────────────┐    │
│  │     Event Loop (SDL_PollEvent)  │    │
│  │     - Keyboard input            │    │
│  │     - Quit events               │    │
│  └─────────────────────────────────┘    │
│                  │                       │
│                  ▼                       │
│  ┌─────────────────────────────────┐    │
│  │     Conditional Redraw          │    │
│  │     - Only when state changes   │    │
│  │     - Clock minute change       │    │
│  │     - Stats change              │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
              │
              │ pthread
              ▼
┌─────────────────────────────────────────┐
│           Stats Thread                   │
│  - Reads /proc/stat (CPU)               │
│  - Reads /proc/meminfo (RAM)            │
│  - Reads thermal zone (Temp)            │
│  - Reads statvfs (Disk)                 │
│  - Updates every 2 seconds              │
│  - Sets changed flag on delta           │
└─────────────────────────────────────────┘
```

## License

MIT License - see LICENSE file for details.
