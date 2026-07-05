# Angerblast

A vertical shoot-'em-up roguelite written in C with [raylib](https://www.raylib.com/) —
**Raiden / Touhou × Vampire Survivors**. Built in the format of the *raylib 6.x* game
jam: a fixed **720×720** arena, single source file, no external asset files (all sound is
generated procedurally and the starfield is drawn, so the build stays self-contained).

Enemies pour from the **top** in Galaga/Raiden-style formations and patterns. Your ship
**auto-fires straight up** — you just move and dodge. Kills drop **XP gems** that fall and
magnet toward you; collecting XP **levels you up**, and each level-up lets you **pick 1 of
3 stacking upgrades**. Each stage is a run of formations that **pauses for a boss** — kill
the boss to advance to a harder stage with denser swarms and fancier (Touhou flower /
danmaku) bullet patterns.

## Controls

| Input | Action |
|-------|--------|
| `WASD` / Arrows / Left stick | Move (gun auto-fires up) |
| Mouse / Right stick | Aim (for the Aim Cannon upgrade) |
| `←` `→` / `1` `2` `3` / D-pad | Choose upgrade on level-up |
| `Space` / `Enter` / Click / Cross | Confirm / start / retry |

Full **PS5 / DualSense** support (USB or Bluetooth). Rumble is wired for hits and boss
kills; note that some Linux raylib builds report `GamepadSetVibration not available` and
silently skip rumble — movement, aim, and menus still work on the pad.

## Enemy types

Data-driven table (`ETYPES` in [src/game.c](src/game.c)) — ~13 distinct types spanning
Space Invaders, Galaga, Galaxian, Centipede, Robotron, Raiden, R-Type and Touhou, each
with its own shape, movement (march, dive, weave, snake, hunt, hover, orbit, drift) and
fire pattern (aimed, spread, ring, rotating flower, spiral). Tougher types unlock and all
enemies scale up in later stages. Adding more is just another row in the table.

## Weapons / upgrades

The main gun auto-fires up; upgrades add stacking auto-weapons that snowball into an OP
build: **Spread, Rapid Fire, Heavy Rounds, Piercing, Velocity Coils, Orbital blades,
Chain Lightning, Side Guns, Tail Gun, Homing Swarm, Damage Aura, Aim Cannon, Auto-Aim Gun,
Missiles, Flamethrower, Flammable Goo (burning puddles), Ion Cannon**, plus survivability
stats (move speed, max shield/HP, shield regen, pickup radius, XP gain).

## Building & running

Developed on an immutable OS (Aurora / Fedora Atomic), so the toolchain lives in a
**distrobox** container instead of being layered onto the host.

### One-time setup

```bash
distrobox create --name raylibbox --image registry.fedoraproject.org/fedora:41 --yes
distrobox enter raylibbox -- sudo dnf install -y gcc make raylib raylib-devel mesa-libGL-devel \
    pulseaudio-libs alsa-lib
```

### Build & play

```bash
./run.sh
```

or manually:

```bash
distrobox enter raylibbox -- make run
```

The window appears on your normal desktop (the container shares the display socket).

## Web build (WebAssembly)

Builds with [emscripten](https://emscripten.org/) to a browser-playable WASM. The game loop
is shared between native and web via `frame()` (web uses `emscripten_set_main_loop`); the
offline bot/batch/search modes are native-only (`#ifndef PLATFORM_WEB` — they need `fork()`).

**One-time toolchain setup** (inside the `raylibbox` distrobox):

```bash
git clone https://github.com/emscripten-core/emsdk ~/emsdk
(cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest)
git clone https://github.com/raysan5/raylib ~/raylib
(source ~/emsdk/emsdk_env.sh && cd ~/raylib/src && emmake make PLATFORM=PLATFORM_WEB)
```

**Build & play in a browser:**

```bash
./run-web.sh          # builds web/angerblast.html + serves on http://localhost:8099
```

or manually: `source ~/emsdk/emsdk_env.sh && make web` (override the raylib path with
`make web RAYLIB_WEB=/path/to/raylib/src`). Output lands in `web/` (`.html .js .wasm .data`);
the `.data` is the preloaded `assets/` TOML. Serve it (browsers won't run `file://` wasm):
`python3 -m http.server -d web`.

## Layout

```
src/game.c   - entry point, the shared frame() loop, offline bot/batch/search modes
Makefile     - desktop build (pkg-config / -lraylib) + `web` (emscripten) target
run.sh       - build & run native inside the distrobox
run-web.sh   - build the WASM + serve it locally
web/         - WASM build output (.html/.js/.wasm/.data)
```
