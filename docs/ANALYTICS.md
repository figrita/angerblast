# Angerblast Balance Analytics

A catalogue of metrics for tuning the game. Each metric notes **where in the code** the
event fires, so instrumentation is a matter of incrementing a counter at a known site.

The natural home for all of this is a per-stage `StageStats` struct plus a `RunStats`
roll-up, reset in `boss_die()` (stage boundary) and `game_reset()` (run boundary). Almost
every metric below is "a counter or accumulator bumped at one specific call site."

The four you already asked for:

- **Average enemy lifespan** — stamp `e->spawnTime = w->runTime` in `enemies_spawn`; in
  `enemies_kill` accumulate `runTime - spawnTime` and divide by kills at stage end.
- **Percent of enemies killed** — `kills / spawned` per stage (track `spawned++` in
  `enemies_spawn`, count leaked-off-bottom separately in `enemies_update`'s despawn branch).
- **Player damage taken** — accumulate in `player_damage` (split shield vs hull, below).
- **Lowest shield % of stage** — sample `min(shield/shieldMax)` each frame in
  `player_update_movement`.

---

## A. Survivability & defense (`player.c`)

1. **Hull HP lost per stage** — count `p->hp--` events in `player_damage`.
2. **Shield damage absorbed per stage** — sum the `amount` subtracted from `p->shield`.
3. **Shield breaks per stage** — count transitions where shield hits exactly 0.
4. **Time spent at zero shield** — accumulate dt while `shield <= 0`; the danger window.
5. **Average shield % across the stage** — running mean, complements your "lowest %".
6. **Shield uptime %** — fraction of stage with `shield == shieldMax` (fully regen'd).
7. **Shield regen efficiency** — total shield regen'd vs total absorbed; if regen ≫ damage,
   shield is over-tuned.
8. **Hits taken during i-frames (wasted hits)** — count `player_damage` early-returns on
   `p->iframe > 0`; high values mean i-frames are carrying the player.
9. **Contact deaths vs bullet deaths** — tag the death by whether the killing
   `player_damage` came from `enemies_update`/boss contact or a bullet collision.
10. **Closest call** — minimum `hp` reached before the stage ended (did they 1-HP it?).
11. **Mean enemy proximity** — average distance of nearest enemy to player per frame;
    measures how crowded the player feels.

## B. Dodge / barrel-roll (`player.c`)

12. **Dodges per stage** — count successful rolls (the `input_dodge_pressed` branch firing).
13. **Dodge attempt-while-on-cooldown count** — count presses rejected by `rollCooldown`;
    high = cooldown frustration.
14. **Dodge cooldown uptime %** — fraction of stage with `rollCooldown > 0`.
15. **Damage avoided by dodge i-frames** — count `player_damage` returns that happened while
    `rollTimer`-granted iframes were active; the dodge's real value.
16. **Average distance dashed** — sum roll displacement; tunes `DODGE_SPEED`/`DODGE_TIME`.

## C. Enemy population & lethality (`enemies.c`, `spawn.c`)

17. **Enemies spawned per stage** — `enemies_spawn` counter (denominator for kill %).
18. **Enemies leaked off-bottom** — the `e->pos.y > SCREEN+60` despawn branch in
    `enemies_update`; leaks are unpunished escapes worth knowing about.
19. **Kills by source breakdown** — in `enemies_kill`, tag the killer: direct bullet
    (`target_damage`), AoE (`enemies_damage_near_fx`), or DoT (`enemy_tick_effects`).
20. **% of kills from damage-over-time** — isolates how much DoT builds carry.
21. **Average enemy HP at death** — most die at ~0, but overkill = `-hp` at death; large
    overkill means weapons are over-tuned for the roster.
22. **Per-enemy-type kill counts & lifespans** — index by `e->type`; surfaces which roster
    entries are trivial vs sponges.
23. **Per-enemy-type leak rate** — which types most often reach the bottom (too fast?).
24. **Enemy bullets fired per stage** — count `bullets_add` with `WK_ENEMY` in `emit_radial`
    / `emitter_fire`; raw danmaku volume.
25. **Enemy bullet hit rate** — enemy-bullet player-hits ÷ enemy bullets fired; the true
    "bullet hell pressure" number.
26. **Average on-screen enemy count** — mean of `enemies_count_active` per frame vs the
    `fieldCap`; shows whether the cap is actually binding.
27. **Field-cap saturation %** — fraction of frames at `enemies_count_active >= fieldCap`
    (waves being throttled in `director_update`).
28. **Contact-damage events** — count the `dist < radius+radius` player-damage hits in
    `enemies_update`; melee pressure vs ranged pressure.

## D. Elites (`enemies.c`, `spawn.c`)

29. **Elite pairs spawned per stage** — count `enemies_spawn_elite_pair` calls.
30. **Elite kill rate** — did the player clear both elites before the next wave?
31. **Average elite lifespan** — elites filtered from metric #22; the miniboss difficulty.
32. **Damage taken during elite waves** — `player_damage` accumulated while an elite is
    active; isolates the elite-moment spike.

## E. Boss fights (`boss.c`)

33. **Boss time-to-kill per stage** — time from `boss_spawn` to `boss_die`.
34. **Boss DPS dealt** — `hpMax / TTK`; compare to expected build DPS.
35. **Damage taken during boss fight** — `player_damage` during `boss.active`.
36. **Boss phase dwell time** — seconds spent in each `b->phase` (single-body bosses);
    are later phases ever reached?
37. **Per-part TTK & order killed** — for multi-part bosses, time to down each
    `BossPartState` and the sequence (`boss_damage_part`).
38. **"Clink" count** — head-shielded no-damage hits in `boss_damage_part`; wasted fire
    against a multi-part boss = targeting/teaching problem.
39. **Boss-grace timeouts** — count stages where the boss spawned via the
    `waveTimer <= -BOSS_GRACE_SECONDS` fallback rather than a cleared field.

## F. Offense, weapons & runes (`weapons.c`, `bullets.c`)

40. **Player bullets fired per stage** — count friendly `bullets_add`.
41. **Player accuracy** — friendly bullet hits ÷ fired; piercing shots count multi-hits.
42. **DPS dealt per stage** — total friendly damage applied ÷ stage duration.
43. **Damage share per weapon slot** — attribute damage by `weaponKind` / source weapon;
    finds dead weapons in the loadout.
44. **Crit rate & crit damage share** — count `b->crit` hits and their damage (Marksman).
45. **Pierce utilization** — average enemies hit per piercing bullet (`pierceLeft` consumed).
46. **Ricochet bounces realized** — `bounceLeft` actually consumed vs granted.
47. **Status-effect application counts** — per `EffectId`, count `enemies_apply_effect`
    calls (burn/slow/freeze/poison/goo/weaken/mark/shock); which effects see play.
48. **Goo-ignite combo rate** — count the `EFFECT_BURN` on a gooed enemy branch (the 2× DoT
    payoff) vs total burns; measures combo reward realization.
49. **Puddle uptime & puddle kills** — active `Puddle` count over time and DoT kills inside.
50. **Weapon evolutions formed & at what stage** — count `evoBanner` triggers and the stage;
    are players evolving on time, late, or never?
51. **Socket fill rate** — average filled sockets across the loadout at stage end.
52. **Rune color distribution** — tally of each `RUNE_*` placed; reveals dominant strategies.

## G. Economy: XP, gems, pickups (`gems.c`, `pickups.c`, `upgrades_sys.c`)

53. **Gems dropped vs collected vs leaked** — `gems_drop` count, pickups in `gems_update`,
    and the `pos.y > SCREEN+20` despawn branch. Leak % = wasted XP.
54. **Gem-pool-overflow merges** — count the "pool full, merge value" branch in `gems_drop`;
    if it ever fires, `MAX_GEMS` is too small for the spawn rate.
55. **XP earned per stage & XP/minute** — sum `xp_gain`; the leveling pace curve.
56. **Level-ups per stage** — count `cs->pending` increments; pacing of card offers.
57. **Average gem travel time** — time from `gems_drop` to collection; tunes
    `pickupRadius`/magnet feel.
58. **Pickups spawned vs collected by kind** — `pickups_drop` vs `pickup_collect`, indexed by
    `PickupKind`; are Board-Wipes ever picked up, or do they fall off-screen?
59. **Frenzy uptime %** — fraction of stage with `frenzyTimer > 0`.
60. **Board-Wipe kills** — enemies cleared by the `PICK_WIPE` `enemies_damage_near` blast;
    measures how much the wipe trivializes a moment.

## H. Pace, flow & meta (`game.c`, `director.c`)

61. **Stage clear time** — wall-clock per stage (`runTime` delta across `boss_die`).
62. **Idle / no-threat time** — dt accumulated with zero active enemies and no boss; dead air
    between waves that may need tightening (`WAVE_INTERVAL_BASE`).
63. **Time in modal screens** — seconds in `STATE_LEVELUP` / `STATE_PAUSE` / `STATE_CODEX`;
    how often flow is interrupted (and decision paralysis on cards).
64. **Card-choice latency** — time from entering `STATE_LEVELUP` to `card_choose`.
65. **Card pick distribution** — which `CardId` gets chosen vs offered; reveals trap/dead
    cards and auto-picks.
66. **Death stage histogram** — at which `stage` runs end; the difficulty wall.
67. **Cause-of-death tag** — enemy type / boss / bullet that dealt the final blow.
68. **Run length & furthest biome** — `runTime` and max `biome` reached at death.
69. **DPS-vs-incoming-HP ratio per stage** — your total damage output vs total enemy+boss HP
    that existed; the single best "is this stage tuned?" sanity number.
70. **Screen-shake / hitflash budget** — time `shakeAmount`/`hitFlash` were nonzero; a proxy
    for how chaotic a stage *felt* (juice vs readability).

---

## Implementation sketch

```c
// world.h — add to GameWorld
typedef struct {
    int   spawned, killed, leaked;
    double enemyLifeSum;          // for avg lifespan
    float hullLost, shieldAbsorbed;
    float minShieldPct;          // init 1.0 each stage
    double shieldPctSum; int shieldSamples;  // for avg
    int   dodges, dodgesOnCooldown;
    int   bulletsFired, bulletHits;
    double damageDealt, xpEarned;
    int   killsByDot, killsByAoe, killsByDirect;
    float bossTTK, dmgDuringBoss;
    // ... per-type arrays, per-effect arrays, etc.
} StageStats;
```

- **Reset**: zero `StageStats` (and set `minShieldPct = 1`) in `boss_die()`, after logging.
- **Per-frame samplers** (shield %, on-screen count, proximity): one block at the top of
  `update_play`.
- **Event counters**: a single `w->stats.X++` at each call site named above.
- **Output**: dump a CSV row per stage to stdout/file at `boss_die`, and a run summary at
  `game_reset`/`STATE_DEAD`. CSV → spreadsheet → balance curves with zero extra tooling.

Start with the ~15 highest-signal rows (#69 DPS-vs-HP, #1–6 survivability, #17/18 kill %,
#33–35 boss, #53/55 economy) — those alone will tell you which stages are over/undertuned.

---

## IMPLEMENTED (the evidence-based balancing loop)

This is now live. The full system:

- **`src/data/analytics.h`** — the `Analytics` struct (whole-run counters + a per-stage
  `StageRow[]` progression array + the survey fields). Lives in `GameWorld.analytics`.
- **`src/systems/analytics.{c,h}`** — lifecycle + report:
  - `analytics_reset()` in `game_reset` (new run), `analytics_sample()` per frame in
    `update_play` (shield %, on-screen count, proximity, idle/shake/frenzy uptime,
    field-cap saturation), `analytics_stage_cleared()` in `boss_die` (per-stage row).
  - `analytics_write_report()` writes a self-contained **Markdown** report to `reports/run-<timestamp>.md`.
- **Instrumentation** — counters bumped at the call sites named above across player.c,
  enemies.c, spawn.c, boss.c, bullets.c, weapons.c, gems.c, pickups.c, upgrades_sys.c.
  `player_damage(w, amount, source)` and `enemies_kill_src(w, e, src)` gained a source tag
  for cause-of-death / kill-source breakdowns.
- **Survey** — `STATE_SURVEY` (render_survey): on death, a 4-axis 1–5 rating form
  (difficulty / fun / pacing / build-power, d-pad + number keys) plus a typed notes field,
  then SUBMIT writes the report and proceeds to the death screen.
- **Triggers** — death → survey → report. Quitting from the title menu (Esc) writes a
  "quit" report if a run happened and wasn't already reported. Esc on the death screen
  returns to the title.

**The loop:** play → die/quit → `reports/run-*.md` is written → hand that file to an LLM
("propose balance.toml / biome edits to address the issues below") → apply its diff → F5 to
hot-reload (for pure data) or rebuild (for code) → play again. The report embeds the current
`balance.toml` values so the LLM always sees exactly what it's editing.

`reports/` is created at runtime; it's throwaway output (safe to delete / gitignore).
