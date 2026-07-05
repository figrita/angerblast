# Angerblast — Design Wishlist

Systems and features to pull Angerblast from "competent survivors clone" toward the depth
that makes Vampire Survivors / Brotato / Megabonk / Death Must Die / Halls of Torment /
20 Minutes Till Dawn sticky. Each item notes **what it is**, **why it matters**, **where it
plugs into our code**, and a rough **effort** (S/M/L).

## Where we are today

Already have: move + always-auto-fire-up main gun; ~17 stacking upgrades (weapons +
global mults); 13 enemy types (data-driven table); formation waves that pause for a stage
boss; XP gems that fall + magnet; pick-1-of-3 on level-up; shield-over-HP; spatial grid;
DualSense + rumble; procedural audio.

What we're missing vs. the genre leaders: **build identity** (everything stacks, nothing
forces commitment), **meta-progression** (every run starts identical, nothing banks),
**interesting decisions** (no reroll/banish/lock, no risk/reward levers), **economy** (no
gold, no shop), **character variety**, **evolution payoff spikes**, and **a difficulty
ladder** for replay.

---

## Tier 1 — Highest impact, fits what we have (do these first)

### 1.1 Reroll / Banish / Skip on the level-up screen  ·  S–M
**What:** Buttons on the card screen: **Reroll** (redraw the 3), **Banish** (remove an
upgrade from this run's pool forever), **Skip** (take nothing, gain bonus XP). Per-run
budget of uses (e.g. 2 each), later expandable by meta.
**Why:** The single most-copied "give players control over RNG" lever in the genre. Cheap
to build, immediately deepens every level-up. Banish in particular lets players *steer*
toward a build.
**Where:** `Cards` struct in [world.h](../src/world.h) gains `rerolls/banishes/skips` +
a `banned[UP_COUNT]` mask; `cards_roll` in [upgrades_sys.c](../src/systems/upgrades_sys.c)
skips banned ids; new input handling + buttons in the STATE_LEVELUP block of
[game.c](../src/game.c) and [render.c](../src/platform/render.c).

### 1.2 Weapon caps + upgrade-vs-stat tension  ·  M
**What:** Cap owned **weapons at 6** and **passive stats at 6** (VS's core constraint).
Once a weapon is maxed it stops being offered. When slots are full, weapon cards only show
for weapons you already own (level-ups), not new ones.
**Why:** Right now everything stacks infinitely, so there's no commitment and no identity —
every run trends to "take all weapons." A hard cap forces a *build*.
**Where:** add per-weapon `level` + a max to the player weapon fields (already counters in
`Player`); `cards_roll` filters by owned-count/cap; tiny bookkeeping in
[upgrades_sys.c](../src/systems/upgrades_sys.c). Consider promoting weapon levels into a
small `Weapon weapons[6]` array for cleanliness (L refactor, optional).

### 1.3 Weapon evolutions (the payoff spike)  ·  M
**What:** A maxed weapon + a specific owned passive **evolves** into a stronger form when
you open a **boss-dropped chest**. E.g. *Chain Lightning (max) + Velocity Coils → Storm
Lattice* (zaps everything on screen); *Flamethrower (max) + Damage → Inferno Wall*;
*Orbital (max) + Bullet Speed → Saw Halo*.
**Why:** This is the genre's dopamine peak — the moment "the build comes online." Gives
bosses a reason to exist beyond a gate.
**Where:** new `data/evolutions.{c,h}` table (base weapon id + required passive →
evolved id); boss death in [boss.c](../src/systems/boss.c) drops a chest entity; opening it
checks the table and swaps weapon fields. Needs an `EVOLVED_*` set of weapon behaviors in
[weapons.c](../src/systems/weapons.c).

### 1.4 Gold + persistent meta shop  ·  M–L
**What:** Destructible enemies / boss chests drop **gold** that **banks across runs**
(written to a save file). A title-screen **shop** spends gold on permanent baseline
upgrades (start +Damage, +1 reroll, +HP, faster magnet, etc.). *No run is ever wasted.*
**Why:** The retention engine of the entire genre — gives losing runs value and a reason to
return. Pairs with the near-miss "just one more run" loop.
**Where:** new `systems/meta.{c,h}` (load/save a `MetaState` struct to
`~/.local/share/angerblast/save.dat`), apply meta bonuses in `player_reset`; gold counter in
`GameWorld`; new title→shop sub-state + render.

### 1.5 Minute/stage-marker formation events  ·  S–M
**What:** Scripted, *learnable* set-pieces layered over the random spawner: a wall of
high-HP enemies that closes in, a bat-swarm line sweep, a pincer from both sides. Fire on a
schedule (e.g. every stage at a fixed wave index).
**Why:** Turns the spawn stream from "noise" into readable, masterable challenge — what
makes VS's clock feel designed rather than random.
**Where:** [spawn.c](../src/systems/spawn.c) — add an event table indexed by
stage/wave-count alongside `spawn_formation`; reuse existing formation builders.

---

## Tier 2 — Build depth & interesting decisions

### 2.1 Character select (stat-template + starting weapon)  ·  M
**What:** 4–6 ships, each a **starting weapon + a signature trait with a real tradeoff**.
e.g. *Gunner* (starts Side Guns, +fire rate, −HP); *Juggernaut* (big shield, −move speed,
+contact dmg); *Engineer* (starts Orbital, weapons +1 level but −25% damage); *Glass*
(huge damage, 1 HP no shield).
**Why:** One shared content pool → many distinct runs. The genre's main replay engine, and
cheap once upgrades exist.
**Where:** `data/characters.{c,h}` table; pick screen before STATE_PLAY; `player_reset`
takes a character id and seeds weapon + trait modifiers.

### 2.2 Costed keystones (downside-bearing upgrades)  ·  S
**What:** A handful of high-power cards with explicit drawbacks: *Overclock* (+50% fire
rate, −1 max HP); *Glass Rounds* (+80% damage, shield no longer regens); *Recoil Engine*
(+homing missiles, −move speed). Mark them visually (red border).
**Why:** Makes "best build" non-obvious and commitment meaningful — the recurring trick in
Brotato/20MTD. Trivial to add to the existing upgrade switch.
**Where:** new ids in [upgrades.c](../src/data/upgrades.c) + effects in `upgrade_apply`
([upgrades_sys.c](../src/systems/upgrades_sys.c)); a `bool costed` flag for the red border.

### 2.3 Prereq-gated synergy upgrades  ·  M
**What:** Upgrades that **only appear once you own a prerequisite combo**. e.g. *Plasma
Storm* (needs Chain Lightning + Ion Cannon) chains the ion beam; *Napalm* (needs Flame +
Goo) makes puddles ignite for burst. The strongest need 2–3 prereqs.
**Why:** Converts "random draft" into "deliberate plan" — you steer toward a keystone. This
is what gives late builds identity (20MTD's signature system).
**Where:** add a `prereq` predicate per upgrade; `cards_roll` only offers a synergy if its
prereqs are met. Small table in [upgrades_sys.c](../src/systems/upgrades_sys.c).

### 2.4 Curse / opt-in difficulty stat  ·  S
**What:** A **Curse** card (and/or a pre-run dial) that raises enemy count/speed/HP **but
multiplies XP and gold**. Player chooses to crank it because a strong build converts the
risk into farming throughput.
**Why:** The cleanest risk/reward lever in the genre; lets confident players push their own
difficulty for reward without us hand-tuning.
**Where:** a `curse` float in `GameWorld` feeding the scaling in
[spawn.c](../src/systems/spawn.c) (field cap, interval) and enemy HP in
[enemies.c](../src/systems/enemies.c), and gem value/`xpMul`.

---

## Tier 3 — Longevity & polish

### 3.1 Difficulty/ascension ladder  ·  S–M
**What:** Unlock harder tiers after a win (Brotato's Danger / VS's Inverse / 20MTD's
Darkness). Each tier multiplies enemy power and unlocks at the title screen. Optionally mix
in *systemic* nerfs at high tiers (one fewer card choice, −XP) so the top forces real
optimization.
**Why:** Content longevity from modifiers, not new assets.
**Where:** a `difficultyTier` in meta save, applied as global multipliers in `player_reset`
/ scaling functions.

### 3.2 Active dodge / dash  ·  M
**What:** A short cooldown **dash** (i-frames or just repositioning) on a button / B / R1.
Optionally statted later (charges, distance) à la Death Must Die.
**Why:** Adds skill expression and a panic button without abandoning auto-fire. Strong feel
upgrade.
**Where:** input in [input.c](../src/platform/input.c); dash state + i-frames in
[player.c](../src/systems/player.c); a few `Player` fields.

### 3.3 Pickup variety (screen-clear, freeze, heal, magnet-all)  ·  S
**What:** Rare drops: **Nuke** (clear all enemy bullets + damage all), **Freeze** (stop
enemies ~3s), **Repair** (heal/refill shield), **Vacuum** (pull all gems). Drop from elites
/ light sources.
**Why:** Moment-to-moment relief + dopamine; the VS "rosary/orologion" beat.
**Where:** a small `Pickup` pool (mirror `gems`), collected like gems in
[gems.c](../src/systems/gems.c)-style update; effects hit existing pools.

### 3.4 Elite enemies + chests  ·  M
**What:** Occasional **elite** variants (tankier, glowing, modifier like "splits on death"
or "shielded") that **drop a chest** (gold + a chance at a weapon evolution). A flag on the
existing `Enemy`.
**Why:** Gives the swarm texture and a mid-run loot beat; the delivery vehicle for §1.3
evolutions outside boss fights.
**Where:** `bool elite` already exists on `Enemy`; spawn logic in
[spawn.c](../src/systems/spawn.c); chest entity + open logic shared with boss chests.

### 3.5 More enemy behaviors & a real bestiary  ·  S each
**What:** New movement/fire entries in the data table: *splitter* (spawns 2 on death),
*shielded* (front-arc immune), *teleporter*, *bomb-on-death*, *tractor* (pulls you). New
danmaku: *aimed ring*, *expanding spiral*, *laser sweep*.
**Why:** We have the data-driven table — adding variety is mostly content, high value per
hour.
**Where:** rows in [enemy_types.c](../src/data/enemy_types.c) + new `MV_`/`FR_` cases in
[enemies.c](../src/systems/enemies.c).

---

## Suggested order

1. **§1.1 Reroll/Banish/Skip** — small, immediately makes level-ups interesting.
2. **§1.2 Weapon caps** — forces build identity; prerequisite for evolutions & synergies.
3. **§2.2 Costed keystones** + **§2.3 synergies** — depth on top of the cap, low cost.
4. **§1.5 formation events** + **§3.5 enemy variety** — content that makes waves feel designed.
5. **§1.4 Gold + meta shop** — the retention layer (bigger lift: needs save I/O).
6. **§1.3 evolutions** + **§3.4 elites/chests** — the payoff-spike layer.
7. **§2.1 characters**, **§3.1 difficulty ladder**, **§3.2 dash**, **§3.3 pickups** — variety & polish.

## Notes / open questions
- **Save format:** a tiny binary or key=value text file under `$XDG_DATA_HOME/angerblast/`.
  Keep it versioned so adding fields later doesn't wipe saves.
- **Weapon-slot refactor:** §1.2/§1.3 are cleaner if the loose `wOrbital/wFlame/...` ints
  become a `Weapon weapons[6]` array (id, level). Worth doing before evolutions pile on.
- **Aim already exists** for the Aim Cannon; a "manual-fire / focus mode" character could
  reuse it if we ever want a 20MTD-style skill option.
