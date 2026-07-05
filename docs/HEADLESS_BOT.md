# Headless bot mode & the self-balancing spine

The groundwork for a self-playtesting game: a deterministic, window-free sim that a heuristic
bot pilots at ~100k fps, dumping the full analytics report each run. This is the foundation the
Monte-Carlo strategy search will build on.

## Run it

```
./angerblast --headless [--seed N] [--frames M]
```

- No window, no audio device, fixed `dt = 1/60`. The **BOT** pilots.
- Runs until the player dies or `M` frames elapse (default ~36000 = 10 sim-minutes).
- Writes `reports/run-<timestamp>.md` (the same full report as a human run) and prints a
  one-line summary (seed, end frame, stage, level, cause).

## The pieces

- **Deterministic RNG** (`core/common.h` `Rng` + `world.h` `frand/frandr/irand`). The RNG is
  **owned by `GameWorld`** and seeded per run in `game_reset` (headless: from `--seed`;
  human: time-based). All gameplay RNG goes through `w->rng` — never raylib's global RNG
  (the one exception is one-time audio noise synthesis, which is cosmetic and uses `rand()`).
  **Same seed ⇒ byte-identical run** (verified). This is what makes Monte-Carlo forking valid:
  each fork carries its own RNG stream and diverges cleanly.
- **Fixed-timestep headless loop** (`run_headless` in `game.c`): drives `update_play` directly,
  resolving level-up cards via the bot, with no rendering.
- **Pilot seam** (`systems/pilot.{c,h}`): the sim calls `pilot_move/dodge/aim/pick_card/
  pick_socket` instead of `input_*` directly. `PILOT_HUMAN` forwards to device input;
  `PILOT_BOT` runs the heuristic. This is the seam that lets the game play itself.
- **Heuristic bot**: movement is a **potential field** (repel from incoming enemy bullets
  weighted by closeness × closing speed, repel from enemies/boss/walls, attract to the nearest
  gem when safe, gentle home-bias to lower-centre); **dodge** fires on a close bullet bearing
  down; **card/socket picks** are greedy (complete a trifecta > new weapon/drone > level a
  weapon > passive). It survives to ~stage 2–4 unaided.

## Decisions (so we don't relitigate them)

- **SoA rewrite: NOT done, NOT needed.** Measured **~104k fps** headless on the existing
  array-of-structs code (10× past the 10k target). The cache-friendly struct-of-arrays
  conversion would be a large, risky rewrite of working code for speed we don't need. Revisit
  only if a future feature (e.g. 10k+ simultaneous entities) actually makes the sim the
  bottleneck. The 16×16 spatial `Grid` already handles enemy collision broad-phase.
- **Unfair-death verdict** (`analytics_classify_death`): a per-frame **threat level** (0..1:
  nearby enemy bullets + close foes + low shield) is rung-buffered for ~2s. On death we read
  the mean threat over the last ~1.5s: **low (<0.18) = UNFAIR** (a spike out of calm),
  **high = EARNED** (on the ropes). The report's "Death verdict" section states which, with a
  tuning implication for unfair deaths (prefer telegraphing / lower burst over flat nerfs).

## Offline modes (all in `systems/search.{c,h}`)

```
./angerblast --headless [--seed N] [--frames M]                 # one bot run -> full report
./angerblast --batch N  [--jobs J] [--seed N] [--frames M]      # N runs across J procs -> batch report
./angerblast --search   [--seed N] [--depth D] [--frames M]     # fork the build tree -> tree report
```

- **`--batch`**: runs `N` seeded bot games (seeds `seed..seed+N-1`), fanned across `J`
  processes via `fork()` (default 24), and writes `reports/batch-<stamp>.md` — survival %,
  unfair-death share, death-stage histogram, biome-reach, avg level/stage/DPS-vs-HP. The
  population view: is the game too easy / too hard / unfair for a *skilled* pilot?
  - Multi-process (not threads) on purpose: the enemy roster (`ETYPES`) is a global, so each
    OS process gets its own copy — zero races, no refactor. ~16× speedup measured on 24 cores
    (96 three-minute games in ~6.4s wall).
- **`--search`**: from one seed, `memcpy`-forks the world at each of the first `D` level-ups
  (3 cards each = 3^D branches), plays every branch to the end, and writes
  `reports/forksearch-<stamp>.md` — a table of card PATHS sorted by outcome. Maps which builds
  dominate vs. die vs. go unfair. (The world is pointer-free, so a fork is just a struct copy.)

Both reports end with **reading guidance** that points at which TOML to tune for each symptom
(too easy → enemy/scaling; unfair deaths → telegraph/soften bursts; DPS≫1 → evolution/rune
magnitudes). The intended loop: **run a batch/search → Claude reads the report → edits any
`assets/*.toml` → re-run.** Claude is the tuner (across every data file), the bot is the tester.

## The loop, run once (worked example)

Goal: a skilled pilot should reach **15-25 min and die close** (not survive forever, not wall
early). Process: edit `assets/*.toml` → `--batch 48 --jobs 24 --frames 90000` (25-min cap) →
read the batch report → repeat. Each batch is ~90s wall on 24 cores.

What the evidence taught (each a real surprise the numbers exposed):
- **The bot takes ~0% bullet hits.** Sparse danmaku is trivially dodgeable; raising bullet
  *speed/density* alone barely moved survival. Enemies must *survive long enough to crowd the
  screen* (HP ramp) for pressure to build.
- **`bullet_base` is an EARLY-game knob, not a late one.** Bumping it 110→150 walled everyone
  at stage 2. Late difficulty must come from the per-stage / `hp_quadratic` *ramp*, leaving
  base values gentle so a fresh fragile ship survives the opening.
- **Front-loaded deaths ≠ build weakness.** A stage-2 death cluster persisted through XP and
  durability tweaks; it eased once **lulls + formation-clear gem bursts** gave early builds room
  to come online. Pacing fixes doubled as difficulty fixes.
- **Durability scales the whole curve cleanly.** `shield_max 50→70`, `hp 3→4` stretched every
  run ~proportionally without distorting the death-stage shape — the clean "shift right" lever.

Final landing: **avg run 15.6 min, deaths ramp smoothly and peak at stage 7, 15% survive the
25-min cap, 0 unfair / 100% "earned" (on the ropes).** Tuned values live in `balance.toml`
(annotated). The whole rebalance was data edits + two small spawn.c features — no guesswork,
every change measured against 48 bot games.

## Decisions log

- **SoA: not needed** (~100k fps single-thread; see above).
- **Parallelism: multi-process** (globals like `ETYPES` make threads racy; processes are clean).
- **Bot hardened** with safety-sampling (`danger_at` lookahead) before searching, so results
  reflect skilled play. It now survives the frame cap in most seeds — which itself revealed the
  game is currently *too easy for a skilled pilot* (98% survival, 0 unfair deaths in a batch).
