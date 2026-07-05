# Spec: Per-Weapon Gems & Trifecta Evolutions

## The pivot

Upgrades like Pierce, Velocity, extra-projectiles, Damage, Rate stop being **global
passives** and become **colored gems you socket into a specific weapon**. Each weapon has
**3 sockets**. A gem buffs *only* the weapon it's socketed into. But the real hook: when a
weapon's three gems form one of **12 named "trifectas"** (color combos), that weapon
**evolves** into a unique form — and *each weapon has a different evolution for every
trifecta*. Placement is **final for the run**.

This makes the build a puzzle of *where* gems go, not just *which* you collect.

---

## 1. Decisions locked in

- **5 gem colors**, each tied to a stat theme.
- **3 sockets per weapon**, **6 weapons** → 18 sockets max.
- Trifectas are **order-independent** (a weapon's 3 gems as an unordered multiset).
- **12 curated trifectas** out of the 35 possible 3-of-5 multisets.
- **Gems still buff stats** as you fill sockets (partial/non-trifecta fills are NOT wasted);
  completing a *valid* trifecta additionally **evolves** the weapon.
- Gems come from the **level-up screen**: pick a gem, place it into any weapon's open
  socket on the **same screen**. Hovering a gem that would complete a trifecta makes the
  matching weapon **glow**.
- **No swapping / bumping** — once placed, a gem is locked for the run.
- **No tiers / rarity.**

## 2. The five gems

| Color | id | Stat buff (per gem, to the socketed weapon) |
|-------|----|----|
| 🔴 Red    | `GEM_RED`    | **Damage** +25% |
| 🔵 Blue   | `GEM_BLUE`   | **Velocity** +25% (bullet speed / reach) |
| 🟢 Green  | `GEM_GREEN`  | **Multi** +1 projectile / +1 target |
| 🟡 Yellow | `GEM_YELLOW` | **Rate** +20% fire speed |
| 🟣 Purple | `GEM_PURPLE` | **Pierce** +1 |

Buffs stack additively within a weapon (two reds = +50% damage). They apply **whether or
not** the three gems form a trifecta — so a "messy" weapon is still a buffed weapon.

## 3. The 12 trifectas

A trifecta is an unordered multiset of 3 colors. Twelve are "real" (named) — the other 23
possible combos just give their stacked stat buffs with no evolution. Chosen to span
mono-color identity, two-color leans, and one rainbow:

| # | id | Combo | Theme (what the evolution leans toward) |
|---|----|-------|------|
| 1  | `TRI_INFERNO`   | Red Red Red       | pure damage / overkill |
| 2  | `TRI_STREAM`    | Blue Blue Blue    | speed / long-range beams |
| 3  | `TRI_SWARM`     | Green Green Green | maximum projectiles |
| 4  | `TRI_BLITZ`     | Yellow Yellow Yel | machine-gun fire rate |
| 5  | `TRI_LANCE`     | Purple Purple Pur | full pierce / line weapons |
| 6  | `TRI_NAPALM`    | Red Red Yellow    | rapid heavy hits / burn |
| 7  | `TRI_SCATTER`   | Green Green Red   | many strong projectiles |
| 8  | `TRI_RAILGUN`   | Blue Blue Purple  | fast piercing rail |
| 9  | `TRI_TEMPEST`   | Yellow Yellow Grn | dense fast spray |
| 10 | `TRI_VOID`      | Purple Purple Red | piercing heavy spear |
| 11 | `TRI_PRISM`     | Red Green Blue    | balanced "rainbow" form |
| 12 | `TRI_CHAOS`     | Yellow Purple Blue| utility / weird form |

(Exact combos are tunable; the data table is the source of truth. Matching is by sorted
color counts so order never matters.)

## 4. Per-weapon × per-trifecta evolutions

The payoff matrix: **6 weapons × 12 trifectas = up to 72 evolved forms.** Each cell is a
distinct evolved behavior. We don't need 72 bespoke code paths on day one — an evolution is
described by a **data row** that tweaks the base weapon's firing (multipliers + a behavior
flag), with a handful of fully-custom showpieces.

```c
typedef struct {
    int   weapon;     // WP_*
    int   trifecta;   // TRI_*
    const char *name; // e.g. "Ion Cannon -> Annihilator"
    EvoMods mods;     // damage/rate/multi/pierce/size multipliers + special behavior id
} Evolution;
```

`EvoMods` carries big multipliers and an optional `special` enum (e.g. SPECIAL_SCREEN_BEAM,
SPECIAL_RING_BURST, SPECIAL_NOVA) so the showpiece trifectas get unique behavior while the
rest are strong stat-shifted versions of the base weapon. Lookup is a flat table; missing
cells fall back to a generic "amped" form so every weapon×trifecta does *something*
distinct-feeling.

## 5. Data model

```c
#define MAX_WEAPONS 6
#define SOCKETS     3

typedef enum { GEM_NONE=-1, GEM_RED, GEM_BLUE, GEM_GREEN, GEM_YELLOW, GEM_PURPLE, GEM_COUNT } GemId;

typedef struct {
    int   type;             // WP_* index into weapon registry
    int   level;            // still upgradable
    int   gems[SOCKETS];    // GemId per socket, or GEM_NONE
    int   evolution;        // TRI_* once a trifecta forms, else -1
    float timer;            // per-weapon fire cooldown (moved off Player)
    bool  active;
} Weapon;

typedef struct { Weapon items[MAX_WEAPONS]; int count; } Loadout;
```

`Player` keeps global survivability (HP, shield, move speed, magnet, XP). The per-weapon
mod ints (`wOrbital`, `spreadLevel`, `globalDamageMul`, …) are removed.

**Effective stats** are computed at fire time from level + gems (+ evolution mods):
```c
typedef struct { float damageMul, rateMul, velMul, sizeMul; int pierce, multi; } WeaponStats;
WeaponStats weapon_stats(const Weapon *wp);
```

**Trifecta detection** (on each gem placement): sort the 3 gem colors into a count vector,
look it up in the trifecta table → set `wp->evolution`, play a fanfare, spawn fx.

## 6. Level-up flow (one screen)

On level-up the screen offers a small hand (e.g. 3 options) drawn from:
- a **gem** (red/blue/green/yellow/purple),
- a **new weapon** (if a weapon slot is free),
- a **weapon level-up** (a weapon you own),
- occasionally a **player passive** (move/HP/shield/magnet — these stay global cards).

If you pick a **gem**, the screen switches to / reveals the **loadout panel**: all 6 weapons
in a row, each showing its 3 sockets (filled gems colored, empties hollow). You move a
cursor over an **open socket** and confirm to place. **Glow rule:** while choosing a socket,
any weapon whose currently-slotted gems + this gem would complete a trifecta **glows** (and
names the evolution), telegraphing the payoff. Placing into a glowing socket triggers the
evolution immediately.

Controller + keyboard: gem choice = the existing card nav; socket placement = move across
weapons/sockets with the d-pad/arrows, confirm with the existing confirm button.

## 7. Code impact (by file)

- **data/gems.{c,h}** (new): `GemDef GEMS[5]` (name, color, stat).
- **data/trifectas.{c,h}** (new): the 12 combos (as sorted count vectors) + names.
- **data/weapons_def.{c,h}** (new): weapon registry (name, base stats, behavior id, color).
- **data/evolutions.{c,h}** (new): the weapon×trifecta evolution table.
- **world.h**: add `Loadout`; strip per-weapon fields from `Player`.
- **systems/weapons.c**: `weapon_stats()` + `weapon_evolution_check()`; rewrite
  `weapons_update` to iterate the loadout and read per-weapon stats/evolution.
- **systems/upgrades_sys.c**: `cards_roll` draws {gem, new-weapon, weapon-levelup, passive};
  `upgrade_apply` splits into grant paths; add `gem_place(w, weaponIdx, socketIdx, gem)`
  with trifecta detection.
- **systems/player.c**: `player_reset` seeds the starting weapon with empty sockets.
- **platform/render.c**: render the loadout panel + sockets + glow; level-up card art.
- **game.c**: STATE_LEVELUP gains a **socket-placement sub-step** (gem chosen → place).

## 8. Migration (always-building steps)

1. Add `Loadout`/`Weapon` + weapon registry; port **one** weapon (Side Guns) to
   `weapon_stats()` with empty sockets. Build, verify identical fire.
2. Port the remaining weapons into the loadout; delete the loose `w*` / global-mult ints.
3. Add gems table + gem stat-folding in `weapon_stats()`. Hardcode-place a test gem; verify
   buff. Build.
4. Add trifecta table + detection + a generic "amped" evolution fallback. Verify a forced
   trifecta evolves.
5. Add the level-up gem-pick + socket-placement UI with the glow rule.
6. Flesh out the evolution table (showpiece specials per weapon×trifecta).

## 9. Tunables / notes
- 12 trifectas vs 35 possible combos: ~34% of full fills evolve. Feels rewarding without
  being automatic. Adjust the named set if it's too easy/hard to hit.
- Mono-color trifectas (RRR/BBB/…) are the most "intentional" (all-in on one stat) and make
  good signature evolutions; mixed ones reward planning across draws.
- Since placement is final, the **glow telegraph is essential** so players aren't blindly
  bricking a weapon. Also show each weapon's *possible* trifectas on hover (stretch).
