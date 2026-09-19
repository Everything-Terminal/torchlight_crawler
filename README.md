# 🗡️ Torchlight Crawler

A tiny fantasy roguelike rendered entirely in the terminal using pure C.

Fight goblins, find potions, and claim the Amulet of Yendor while your torch casts flickering truecolor light into the darkness. No game engines, no external libraries, just C, math, and ANSI escape codes.

---

## 🎥 Watch the Build

See the full ASMR coding session where I build this from scratch:

[![Watch on YouTube](https://img.shields.io/badge/Watch%20on-YouTube-red?style=for-the-badge&logo=youtube)](https://www.youtube.com/watch?v=rWLxiMvAGa0)

---

## ✨ Features

- 🔥 Dynamic truecolor torchlight with warm orange tint
- 🕯️ Sine-wave flicker for realistic fire effect
- 🗺️ Hand-crafted dungeon map with rooms and corridors
- ⚔️ Bump-to-attack turn-based combat
- 👹 Goblins with simple pursuit AI
- 🧪 Health potions scattered throughout the dungeon
- 🏆 Amulet of Yendor win condition
- 🖥️ Flicker-free rendering using buffered output
- 🛡️ Bounds-checked rendering to prevent buffer overflows
- 🧹 Clean exit with terminal restoration via `sigaction`

---

## 📸 What It Does

The game renders a top-down dungeon where the player carries a torch. Only tiles within the torch radius are visible, lit with a warm gradient that fades into darkness.

- Move with `hjkl` or arrow keys
- Walk into a goblin to attack it
- Walk over a potion to heal
- Reach the Amulet to win
- Goblins only move when they are close enough to see your light

---

## 🧱 Requirements

- Linux or macOS terminal
- GCC
- Math library (`-lm`)
- Truecolor-capable terminal

Most modern terminals support truecolor, including GNOME Terminal, Kitty, Alacritty, WezTerm, and tmux with truecolor support.

---

## 🚀 Build

Compile:

```bash
gcc -std=c11 -Wall -Wextra torchlight_crawler.c -o torchlight_crawler -lm
```

Or with GNU extensions:

```bash
gcc -std=gnu11 -Wall -Wextra torchlight_crawler.c -o torchlight_crawler -lm
```

---

## 🎮 Run

```bash
./torchlight_crawler
```

---

## 🕹️ Controls

| Input                 | Action                        |
| --------------------- | ----------------------------- |
| `h` / `j` / `k` / `l` | Move left / down / up / right |
| Arrow keys            | Move (alternate)              |
| `q`                   | Quit                          |
| `Ctrl+C`              | Quit (clean terminal restore) |

---

## 🧠 How It Works

### Torchlight Rendering

For every tile on the map, the renderer computes the Euclidean distance to the player:

```text
dist = sqrt((x - player_x)² + (y - player_y)²)
```

This distance is mapped to a light intensity between 0.0 (pitch black) and 1.0 (fully lit), using the torch radius as the falloff range. A subtle sine-wave flicker modulates the radius each frame to simulate fire.

### Warm Fire Tint

The light is not pure white. Each tile's final color adds extra red and a small amount of green, giving the torchlight a warm orange glow:

```text
final_r = base_r * light + 60 * light
final_g = base_g * light + 20 * light
final_b = base_b * light
```

### Turn-Based Combat

The game is turn-based. Every time the player moves or attacks, all enemies take their turn. Goblins use a simple pursuit algorithm:

1. Compute the distance to the player
2. If the goblin is within the torch radius, it becomes active
3. If adjacent, it attacks the player
4. Otherwise, it steps one tile toward the player

### Safe Buffered Rendering

All output is written to a static buffer using a bounds-checked `buf_append` helper backed by `vsnprintf`. This prevents buffer overflows even if the map or terminal size changes.

### Monotonic Timing

The flicker animation uses `clock_gettime(CLOCK_MONOTONIC)` instead of `clock()`, ensuring the animation runs in real wall-clock time regardless of CPU sleep or process scheduling.

---

## 🎨 Colors

| Element    | Base Color                       |
| ---------- | -------------------------------- |
| Player `@` | Warm white `rgb(255, 255, 150)`  |
| Goblin `g` | Deep red `rgb(180, 40, 40)`      |
| Potion `!` | Bright pink `rgb(255, 100, 200)` |
| Amulet `*` | Cyan `rgb(100, 255, 255)`        |
| Walls `#`  | Stone gray `rgb(80, 80, 90)`     |
| Floor `.`  | Dark earth `rgb(30, 25, 20)`     |

---

## 📁 Project Structure

```text
torchlight-crawler/
├── torchlight_crawler.c
├── README.md
└── .gitignore
```

---

## 🛠️ Possible Future Improvements

- Procedural dungeon generation (BSP or cellular automata)
- Multiple dungeon levels with stairs
- More enemy types (skeletons, bats, dragons)
- Ranged attacks and spells
- Fog of war (remember explored tiles)
- Inventory system
- Sound effects using terminal bell
- Different torch colors (magic torches)
- Boss fight guarding the Amulet
- Score and turn counter
