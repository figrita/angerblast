// analytics.c - run instrumentation lifecycle + the Markdown balance report.
#include "systems/analytics.h"
#include "data/balance.h"
#include "data/biome.h"
#include "data/enemy_types.h"
#include "data/weapons_def.h"
#include "data/effects.h"
#include "data/runes.h"
#include "data/trifectas.h"
#include "data/evolutions.h"
#include "systems/enemies.h"     // enemies_count_active, enemies_nearest
#include "systems/pickups.h"     // pickup_name
#include "systems/drones.h"      // drone names
#include "raymath.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>            // mkdir

void analytics_reset(GameWorld *w) {
    Analytics *a = &w->analytics;
    memset(a, 0, sizeof(*a));
    a->startRunTime   = w->runTime;
    a->curStageStart  = w->runTime;
    a->minHp          = 9999;
    a->minShieldPct   = 1.0f;
    a->deathStage     = -1;
    snprintf(a->causeOfDeath, sizeof(a->causeOfDeath), "alive");
}

void analytics_sample(GameWorld *w, float dt) {
    Analytics *a = &w->analytics;
    Player *p = &w->player;
    a->totalFrames++;

    // shield %
    float sp = (p->shieldMax > 0) ? p->shield / p->shieldMax : 0;
    a->shieldPctSum += sp; a->shieldSamples++;
    if (sp < a->minShieldPct) a->minShieldPct = sp;
    if (p->shield <= 0) a->timeAtZeroShield += dt;
    if (p->shield >= p->shieldMax) a->shieldFullTime += dt;
    if (p->hp < a->minHp) a->minHp = p->hp;

    // dodge cooldown uptime
    if (p->rollCooldown > 0) a->dodgeCooldownTime += dt;

    // on-screen enemy count + field-cap saturation
    int active = enemies_count_active(w);
    a->onScreenSum += active; a->onScreenSamples++;
    BiomeDirector *bd = &BIOMES[w->biome].director;
    int capBase = bd->fieldCapBase ? bd->fieldCapBase : FIELD_CAP_BASE;
    int capPer  = bd->fieldCapPerStage ? bd->fieldCapPerStage : FIELD_CAP_PER_STAGE;
    int capMax  = bd->fieldCapMax ? bd->fieldCapMax : FIELD_CAP_MAX;
    int fieldCap = capBase + w->stage*capPer; if (fieldCap > capMax) fieldCap = capMax;
    if (active >= fieldCap) a->fieldCapFrames++;

    // nearest-enemy proximity (crowdedness)
    int ni = enemies_nearest(w, p->pos, 1e9f, -1);
    if (ni >= 0) { a->proximitySum += Vector2Distance(p->pos, w->enemies.items[ni].pos); a->proximitySamples++; }

    // idle / juice budgets
    if (active == 0 && !w->boss.active) a->idleTime += dt;
    if (w->shakeAmount > 0.1f) a->shakeTime += dt;
    if (w->hitFlash > 0.02f)   a->hitFlashTime += dt;
    if (p->frenzyTimer > 0)    a->frenzyTime += dt;

    // ---- threat level (0..1): how dangerous is RIGHT NOW? feeds the fair/unfair-death verdict.
    // Nearby incoming enemy bullets dominate (bullet-hell pressure), plus close enemies, plus a
    // low-shield term. Capped at 1. The ring buffer remembers the last ~2s.
    float threat = 0;
    for (int i = 0; i < MAX_BULLETS; i++) { Bullet *b = &w->bullets.items[i];
        if (!b->active || b->friendly) continue;
        float d = Vector2Distance(p->pos, b->pos);
        if (d < 140) threat += (1.0f - d/140.0f) * 0.10f; }     // each near bullet adds pressure
    for (int i = 0; i < MAX_ENEMIES; i++) { Enemy *e = &w->enemies.items[i];
        if (!e->active) continue;
        float d = Vector2Distance(p->pos, e->pos);
        if (d < 90) threat += (1.0f - d/90.0f) * 0.08f; }
    if (sp < 0.5f) threat += (0.5f - sp) * 0.6f;                // low shield = more precarious
    if (threat > 1.0f) threat = 1.0f;

    a->threatRing[a->threatHead] = threat;
    a->threatHead = (a->threatHead + 1) % THREAT_RING;
    a->threatSum += threat; a->threatSamples++;
    if (threat > 0.35f) a->timeInDanger += dt;                 // "on the ropes" accumulator
}

// Called at death (from player_damage) to read the threat profile of the last ~1.5s and
// decide whether the death was EARNED (sustained danger) or UNFAIR (a spike out of calm).
void analytics_classify_death(GameWorld *w) {
    Analytics *a = &w->analytics;
    if (a->deathClassified) return;
    int window = 90;   // ~1.5s
    float sum = 0, peak = 0; int n = 0;
    for (int k = 1; k <= window && k <= THREAT_RING; k++) {
        int idx = (a->threatHead - k + THREAT_RING) % THREAT_RING;
        float t = a->threatRing[idx];
        sum += t; if (t > peak) peak = t; n++;
    }
    a->threatBeforeDeath = n ? sum/n : 0;
    a->peakThreatBeforeDeath = peak;
    // Unfair = you were calm right up to death (a sudden spike). Earned = sustained danger.
    // Mean threat under ~0.18 over the prior 1.5s => the death came out of nowhere.
    a->deathWasUnfair = (a->threatBeforeDeath < 0.18f);
    a->deathClassified = true;
}

void analytics_stage_cleared(GameWorld *w) {
    Analytics *a = &w->analytics;
    if (a->stageRowCount < MAX_STAGE_ROWS) {
        StageRow *r = &a->stages[a->stageRowCount++];
        r->stage       = w->stage;
        r->clearTime   = (float)(w->runTime - a->curStageStart);
        r->levelAtClear= w->player.level;
        r->damageDealt = a->damageDealt - a->curStageDamageStart;
        r->incomingHp  = a->incomingHp - a->curStageIncomingStart;
        r->hullLost    = a->hullLost - a->curStageHullStart;
        r->bossTTK     = a->bossFights ? (a->bossTTKSum / a->bossFights) : 0;  // approx (last)
    }
    a->stagesCleared++;
    if (w->biome > a->furthestBiome) a->furthestBiome = w->biome;
    // snapshot for the next stage's deltas
    a->curStageStart        = w->runTime;
    a->curStageDamageStart  = a->damageDealt;
    a->curStageIncomingStart= a->incomingHp;
    a->curStageHullStart    = a->hullLost;
}

// ---- report ----
static const char *star_bar(int n) {   // 1-5 -> "★★★☆☆"-ish in ASCII
    static const char *bars[6] = { "-", "[#----]", "[##---]", "[###--]", "[####-]", "[#####]" };
    if (n < 0) n = 0;
    if (n > 5) n = 5;
    return bars[n];
}

// op-id -> name reverse lookups (mirror the loader's tables) for human-readable dumps
static const char *move_name(int o){ static const char*T[]={"march","dive","weave","crab","snake","hunt","hover","orbit","drift","straight"};
    return (o>=0&&o<10)?T[o]:"?"; }
static const char *fire_name(int o){ return o==FR_NONE?"none":(o==FR_RADIAL?"radial":"?"); }
static const char *emit_name(int o){ static const char*T[]={"straight","fixed_dirs","aimed","autoaim","homing_volley","chain","aura","orbital","cone","lob"};
    return (o>=0&&o<EMIT_COUNT)?T[o]:"?"; }

// Dump every tunable table so the LLM sees current values for ALL of it, not just balance.toml.
// Each section is tagged with WHERE it lives (TOML = data edit + F5; C = code edit + rebuild).
static void dump_tunables(FILE *f, GameWorld *w) {
    (void)w;
    fprintf(f, "# Current tunable values (everything you can balance)\n\n");
    fprintf(f, "> Edit targets are tagged: **[TOML]** = a data file (hot-reload with F5), "
               "**[C]** = a hardcoded constant (edit the source + rebuild).\n\n");

    // ---- enemies (per biome roster) [TOML] ----
    fprintf(f, "## Enemies — `assets/biomes/*/enemies/*.toml` [TOML]\n\n");
    for (int b = 0; b < NUM_BIOMES; b++) {
        fprintf(f, "**Biome %d — %s**\n\n", b, BIOMES[b].name ? BIOMES[b].name : "?");
        fprintf(f, "| Enemy | hp | speed | radius | fire_int | min_stage | xp | move | fire | elite |\n");
        fprintf(f, "|---|---|---|---|---|---|---|---|---|---|\n");
        for (int i = 0; i < BIOMES[b].rosterCount; i++) { const EnemyType *e = &BIOMES[b].roster[i];
            fprintf(f, "| %s | %.0f | %.0f | %.0f | %.2f | %d | %.0f | %s | %s | %s |\n",
                    e->name?e->name:"?", e->hp, e->speed, e->radius, e->fireInterval, e->minStage, e->xp,
                    move_name(e->move), e->emitter>=0?"(emitter)":fire_name(e->fire), e->elite?"yes":""); }
        fprintf(f, "\n");
    }

    // ---- weapons [TOML] ----
    fprintf(f, "## Weapons — `assets/weapons.toml` [TOML]\n\n");
    fprintf(f, "| Weapon | base_interval | base_damage | emit | key emit_params |\n|---|---|---|---|---|\n");
    for (int i = 0; i < WP_COUNT; i++) { const WeaponDef *d = &WEAPONS_DEF[i]; const EmitParams *e = &d->ep;
        fprintf(f, "| %s | %.2f | %.0f | %s | spd×%.1f r%.0f spread%.2f cnt%d pierce%d targets%d range%.0f |\n",
                d->name?d->name:"?", d->baseInterval, d->baseDamage, emit_name(d->emit),
                e->speedScale, e->radius, e->spread, e->count, e->pierce, e->targets, e->range); }
    fprintf(f, "\n");

    // ---- evolutions (mods per trifecta; shared across weapons) [TOML] ----
    fprintf(f, "## Evolution mods — `assets/evolutions.toml` `[mods.*]` [TOML]\n\n");
    fprintf(f, "| Trifecta | dmg× | rate× | size× | +multi | +pierce |\n|---|---|---|---|---|---|\n");
    for (int t = 0; t < TRI_COUNT; t++) { const EvoMods *m = &EVOLUTIONS[0][t].mods;   // mods are shared down a column
        fprintf(f, "| %s | %.2f | %.2f | %.2f | %d | %d |\n", TRIFECTAS[t].name?TRIFECTAS[t].name:"?",
                m->damageMul, m->rateMul, m->sizeMul, m->multiAdd, m->pierceAdd); }
    fprintf(f, "\n");

    // ---- runes (labels [TOML]; the actual stat magnitudes [C]) ----
    fprintf(f, "## Runes — labels in `assets/runes.toml` [TOML]; magnitudes in `weapon_stats()` [C]\n\n");
    fprintf(f, "| Rune | label | effect (hardcoded in weapons.c) |\n|---|---|---|\n");
    fprintf(f, "| %s | %s | +0.25 damageMul |\n", RUNES[RUNE_RED].name, RUNES[RUNE_RED].stat);
    fprintf(f, "| %s | %s | +0.25 velMul |\n", RUNES[RUNE_BLUE].name, RUNES[RUNE_BLUE].stat);
    fprintf(f, "| %s | %s | +1 multi |\n", RUNES[RUNE_GREEN].name, RUNES[RUNE_GREEN].stat);
    fprintf(f, "| %s | %s | ×0.80 rate (faster) |\n", RUNES[RUNE_YELLOW].name, RUNES[RUNE_YELLOW].stat);
    fprintf(f, "| %s | %s | +1 pierce |\n\n", RUNES[RUNE_PURPLE].name, RUNES[RUNE_PURPLE].stat);

    // ---- effects [TOML] ----
    fprintf(f, "## Status effects — `assets/effects.toml` [TOML]\n\n");
    fprintf(f, "| Effect | duration | dot | halts move | halts fire |\n|---|---|---|---|---|\n");
    for (int e = 0; e < EFFECT_COUNT; e++) { const EffectDef *d = &EFFECTS[e];
        fprintf(f, "| %s | %.1f | %s | %s | %s |\n", d->name?d->name:"?", d->defDuration,
                d->isDot?"yes":"", d->haltsMove?"yes":"", d->haltsFire?"yes":""); }
    fprintf(f, "\n");

    // ---- passives [C] ----
    fprintf(f, "## Passive upgrades — magnitudes in `passive_apply()` `upgrades_sys.c` [C]\n\n");
    fprintf(f, "| Passive | effect |\n|---|---|\n");
    fprintf(f, "| Rapid Fire | globalRateMul ×0.85 |\n| Heavy Rounds | globalDamageMul ×1.25 |\n");
    fprintf(f, "| Spread Shot | +1 spread projectile |\n| Velocity Coils | bulletSpeed ×1.2 |\n");
    fprintf(f, "| Thrusters | moveSpeed ×1.12 |\n| Shield Capacitor | shieldMax +25 |\n");
    fprintf(f, "| Reinforced Hull | maxHP +1 |\n| Fast Recharge | regenDelay ×0.7, rate ×1.4 |\n");
    fprintf(f, "| Magnet Coil | pickupRadius ×1.4 |\n| Greed | xpMul ×1.15 |\n| Aegis Rounds | ~35%% of shots pop one enemy bullet |\n\n");

    // ---- pickups [C] ----
    fprintf(f, "## Pickups — magnitudes in `pickups.c` [C]; drop rate in `pickups_maybe_drop()` [C]\n\n");
    fprintf(f, "| Pickup | effect | tuning |\n|---|---|---|\n");
    fprintf(f, "| Magnet | pull all gems | (no magnitude) |\n");
    fprintf(f, "| Board Wipe | clear enemy bullets + AoE | damage = 40 + stage×8 |\n");
    fprintf(f, "| Instant Level | +1 level | (no magnitude) |\n");
    fprintf(f, "| Frenzy | rate ×0.55 + dmg ×1.5 | duration = 8.0s |\n");
    fprintf(f, "- Drop chance per kill: ~2%% (random kind, Board-Wipe halved). Boss: 12 gems + a Frenzy. Hand kill: 4 gems + a random pickup.\n\n");

    // ---- drones [C] ----
    fprintf(f, "## Drones — AIs + default weapons in `drones.c` [C]\n\n");
    for (int d = 0; d < DRONE_AI_COUNT; d++)
        fprintf(f, "- %s → carries %s\n", drone_ai_name(d), WEAPONS_DEF[drone_default_weapon(d)].name);
    fprintf(f, "\n");

    // ---- biomes: director + boss [TOML] ----
    fprintf(f, "## Biomes — director + boss, `assets/biomes/*/biome.toml` [TOML]\n\n");
    for (int b = 0; b < NUM_BIOMES; b++) { Biome *bm = &BIOMES[b]; BiomeDirector *bd = &bm->director;
        fprintf(f, "**%s** — field_cap %d (+%d/stage, max %d), wave_interval %.1f, waves_to_boss %d\n",
                bm->name?bm->name:"?", bd->fieldCapBase, bd->fieldCapPerStage, bd->fieldCapMax,
                bd->waveIntervalBase, bd->wavesToBoss);
        BossDef *bo = &bm->boss;
        fprintf(f, "  - Boss **%s**: hp %.0f, radius %.0f", bo->name?bo->name:"?", bo->hp, bo->radius);
        if (bo->partCount > 0) { fprintf(f, ", %d parts (", bo->partCount);
            for (int p=0;p<bo->partCount;p++) fprintf(f, "%s%.0fhp%s", bo->parts[p].isHead?"head ":"hand ", bo->parts[p].hp, p<bo->partCount-1?", ":"");
            fprintf(f, ")"); }
        fprintf(f, "\n");
    }
    fprintf(f, "\n");
}

void analytics_write_report(GameWorld *w, const char *cause, char *outPath, int cap) {
    Analytics *a = &w->analytics;
    if (outPath && cap) outPath[0] = 0;

    mkdir("reports", 0755);   // ok if it already exists
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
    char path[256];
    snprintf(path, sizeof(path), "reports/run-%s.md", stamp);
    FILE *f = fopen(path, "w");
    if (!f) return;

    double dur = w->runTime - a->startRunTime; if (dur < 0.001) dur = 0.001;
    double mins = dur / 60.0;
    #define PCT(num,den) ((den) > 0 ? 100.0*(double)(num)/(double)(den) : 0.0)
    #define AVG(sum,n)   ((n) > 0 ? (double)(sum)/(double)(n) : 0.0)

    fprintf(f, "# Angerblast Run Report — %s\n\n", stamp);
    fprintf(f, "_Hand this file to an LLM: \"using the current tunable values + analytics below, "
               "propose edits to fix the issues. Tables tagged [TOML] are data files (F5 to hot-reload); "
               "[C] are hardcoded constants (edit source + rebuild).\"_\n\n");
    fprintf(f, "**Run end:** %s   |   **duration:** %.0fs (%.1f min)   |   **stages cleared:** %d   |   **end level:** %d   |   **furthest biome:** %d\n\n",
            cause ? cause : "?", dur, mins, a->stagesCleared, w->player.level, a->furthestBiome);

    // ---- the survey ----
    fprintf(f, "## Player survey\n\n");
    if (a->surveyDone) {
        fprintf(f, "| Axis | Rating (1-5) | |\n|---|---|---|\n");
        fprintf(f, "| Difficulty | %d | %s |\n", a->ratings[0], star_bar(a->ratings[0]));
        fprintf(f, "| Fun | %d | %s |\n", a->ratings[1], star_bar(a->ratings[1]));
        fprintf(f, "| Pacing (1 slow .. 5 fast) | %d | %s |\n", a->ratings[2], star_bar(a->ratings[2]));
        fprintf(f, "| Build power (1 weak .. 5 OP) | %d | %s |\n", a->ratings[3], star_bar(a->ratings[3]));
        fprintf(f, "\n**Notes:** %s\n\n", a->notes[0] ? a->notes : "_(none)_");
    } else {
        fprintf(f, "_(survey skipped)_\n\n");
    }

    // ---- death verdict: the balance objective's core (fair/on-the-ropes vs unfair spike) ----
    fprintf(f, "## Death verdict\n\n");
    if (a->deathClassified) {
        fprintf(f, "- **%s** — killed by **%s** at stage %d.\n",
                a->deathWasUnfair ? "UNFAIR (sudden spike)" : "EARNED (on the ropes)",
                a->causeOfDeath, a->deathStage);
        fprintf(f, "- Threat in the ~1.5s before death: avg **%.2f**, peak **%.2f** (0..1). "
                   "Low avg = the death came out of calm (unfair); high = sustained danger (earned).\n",
                a->threatBeforeDeath, a->peakThreatBeforeDeath);
        fprintf(f, "- Run-wide: avg threat %.2f, time-in-danger %.1fs (%.0f%% of run).\n",
                a->threatSamples ? a->threatSum/a->threatSamples : 0, a->timeInDanger,
                dur > 0 ? 100.0*a->timeInDanger/dur : 0);
        if (a->deathWasUnfair)
            fprintf(f, "- **Tuning implication:** an unfair death suggests a too-sudden threat spike "
                       "(e.g. an offscreen/fast burst, an elite or boss pattern with no telegraph). "
                       "Prefer reducing burst lethality or adding wind-up over flatly nerfing damage.\n");
    } else {
        fprintf(f, "- Survived (no death this run), or death not classified.\n");
    }
    fprintf(f, "\n");

    // ---- global balance.toml (the headline knobs) ----
    fprintf(f, "## Global balance — `assets/balance.toml` [TOML]\n\n```toml\n");
    fprintf(f, "[player] hp=%d shield_max=%.0f shield_rate=%.0f move_speed=%.0f bullet_damage=%.0f\n",
            BAL.hp, BAL.shield_max, BAL.shield_rate, BAL.move_speed, BAL.bullet_damage);
    fprintf(f, "         iframe=%.2f dodge_speed=%.0f dodge_iframe=%.2f dodge_cooldown=%.2f\n",
            BAL.iframe, BAL.dodge_speed, BAL.dodge_iframe, BAL.dodge_cooldown);
    fprintf(f, "[enemy_scaling] hp_linear=%.3f hp_quadratic=%.3f speed_per_stage=%.3f bullet_base=%.0f bullet_per_stage=%.1f\n",
            BAL.hp_linear, BAL.hp_quadratic, BAL.speed_per_stage, BAL.bullet_base, BAL.bullet_per_stage);
    fprintf(f, "[director] field_cap_base=%d field_cap_per_stage=%d field_cap_max=%d\n",
            BAL.field_cap_base, BAL.field_cap_per_stage, BAL.field_cap_max);
    fprintf(f, "           wave_interval_base=%.1f wave_interval_min_sub=%.1f waves_per_stage_base=%d boss_grace_seconds=%.0f\n",
            BAL.wave_interval_base, BAL.wave_interval_min_sub, BAL.waves_per_stage_base, BAL.boss_grace_seconds);
    fprintf(f, "[boss] hp_base=%.0f hp_per_stage=%.0f\n", BAL.boss_hp_base, BAL.boss_hp_per_stage);
    fprintf(f, "[xp] base=%.1f exponent=%.2f\n", BAL.xp_base, BAL.xp_exponent);
    fprintf(f, "```\n\n");
    // ---- and the full per-table tunables (enemies / weapons / evolutions / runes / effects /
    //      passives / pickups / drones / biomes) so the LLM can balance ALL of it ----
    dump_tunables(f, w);

    // ---- the single best sanity number ----
    fprintf(f, "## Headline numbers\n\n");
    fprintf(f, "| Metric | Value | Read |\n|---|---|---|\n");
    fprintf(f, "| DPS-vs-incoming-HP ratio | **%.2fx** | >2x = weapons outscale the roster (too easy); <1x = undertuned |\n",
            a->incomingHp > 0 ? a->damageDealt / a->incomingHp : 0);
    fprintf(f, "| XP / minute | %.0f | leveling pace |\n", AVG(a->xpEarned, mins));
    fprintf(f, "| Level reached | %d in %.1f min | power-vs-time |\n", w->player.level, mins);
    fprintf(f, "| Kill %% | %.0f%% | killed / spawned |\n", PCT(a->killed, a->spawned));
    fprintf(f, "| Enemy-bullet hit rate | %.1f%% | bullet-hell pressure landing on you |\n", PCT(a->enemyBulletHits, a->enemyBulletsFired));
    fprintf(f, "| Player accuracy | %.0f%% | hits / fired |\n", PCT(a->bulletHits, a->bulletsFired));
    fprintf(f, "| Avg on-screen enemies | %.0f | field crowdedness |\n", AVG(a->onScreenSum, a->onScreenSamples));
    fprintf(f, "| Field-cap saturation | %.0f%% | waves being throttled |\n\n", PCT(a->fieldCapFrames, a->totalFrames));

    // ---- per-stage progression ----
    fprintf(f, "## Per-stage progression\n\n");
    fprintf(f, "| Stage | Clear (s) | Lvl@clear | Stage DPS-vs-HP | Hull lost | Boss TTK (s) |\n|---|---|---|---|---|---|\n");
    for (int i = 0; i < a->stageRowCount; i++) { StageRow *r = &a->stages[i];
        fprintf(f, "| %d | %.0f | %d | %.2fx | %d | %.0f |\n", r->stage, r->clearTime, r->levelAtClear,
                r->incomingHp > 0 ? r->damageDealt / r->incomingHp : 0, r->hullLost, r->bossTTK); }
    fprintf(f, "\n");

    // ---- survivability ----
    fprintf(f, "## Survivability & defense\n\n");
    fprintf(f, "- Hull HP lost: **%d**   |   shield absorbed: %.0f   |   shield breaks: %d   |   regen'd: %.0f (efficiency %.0f%%)\n",
            a->hullLost, a->shieldAbsorbed, a->shieldBreaks, a->shieldRegened, PCT(a->shieldRegened, a->shieldAbsorbed));
    fprintf(f, "- Avg shield %%: %.0f%%   |   lowest shield %%: %.0f%%   |   time at 0 shield: %.1fs   |   full-shield uptime: %.0f%%\n",
            100.0*AVG(a->shieldPctSum, a->shieldSamples), 100.0*a->minShieldPct, a->timeAtZeroShield, PCT(a->shieldFullTime, dur));
    fprintf(f, "- Closest call (min HP): %d   |   wasted hits (i-frame): %d   |   contact events: %d   |   mean nearest-enemy: %.0fpx\n\n",
            a->minHp==9999?w->player.hpMax:a->minHp, a->wastedHits, a->contactEvents, AVG(a->proximitySum, a->proximitySamples));

    // ---- dodge ----
    fprintf(f, "## Dodge / barrel-roll\n\n");
    fprintf(f, "- Dodges: **%d**   |   rejected (on cooldown): %d   |   cooldown uptime: %.0f%%   |   hits avoided by dodge i-frames: %d   |   total dash: %.0fpx\n\n",
            a->dodges, a->dodgesOnCooldown, PCT(a->dodgeCooldownTime, dur), a->dodgeAvoidedHits, a->dodgeDistance);

    // ---- enemies ----
    fprintf(f, "## Enemies\n\n");
    fprintf(f, "- Spawned: %d   |   killed: %d (%.0f%%)   |   leaked off-bottom: %d (%.0f%%)   |   avg lifespan: %.1fs   |   avg overkill: %.0f hp\n",
            a->spawned, a->killed, PCT(a->killed,a->spawned), a->leaked, PCT(a->leaked,a->spawned),
            AVG(a->enemyLifeSum, a->killed), AVG(a->overkillSum, a->killed));
    fprintf(f, "- Kills by source — direct: %d, AoE: %d, DoT: %d (%.0f%% DoT)\n",
            a->killsByDirect, a->killsByAoe, a->killsByDot, PCT(a->killsByDot, a->killed));
    fprintf(f, "- Enemy bullets fired: %d   |   contact events: %d\n", a->enemyBulletsFired, a->contactEvents);
    fprintf(f, "- Elite pairs: %d   |   elite kills: %d   |   avg elite lifespan: %.1fs\n\n",
            a->elitePairs, a->eliteKills, AVG(a->eliteLifeSum, a->eliteDeaths));
    // per-type table — the THREAT BALANCE view. `dmg→you` is the key column: it should be
    // roughly EVEN across types. A type with a big share is "too hard"; ~0 with many bullets
    // is harmless/ignorable. `dmg/100shots` = how lethal each of its bullets is (efficiency).
    int totalDmgToYou = 0;
    for (int t=0;t<MAX_ENEMY_TYPES;t++) totalDmgToYou += a->dmgToPlayerByType[t];
    fprintf(f, "| Enemy | kills | avg life | leaks | bullets | dmg->you | %% of dmg | dmg/100shots |\n");
    fprintf(f, "|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < enemy_threat_count(); t++) {   // by stable name slot (correct across biomes)
        if (a->typeKills[t]==0 && a->typeLeaks[t]==0 && a->bulletsByType[t]==0 && a->dmgToPlayerByType[t]==0) continue;
        long blt = a->bulletsByType[t]; int dmg = a->dmgToPlayerByType[t];
        const char *nm = enemy_threat_name(t);
        fprintf(f, "| %s | %d | %.1f | %d | %ld | %d | %.0f%% | %.2f |\n",
                nm ? nm : "?",
                a->typeKills[t], AVG(a->typeLife[t], a->typeKills[t]), a->typeLeaks[t],
                blt, dmg, totalDmgToYou ? 100.0*dmg/totalDmgToYou : 0,
                blt ? 100.0*dmg/blt : 0);
    }
    fprintf(f, "\n- **Goal: even `%% of dmg` across types.** A type hogging the share is too hard "
               "(cut its fire rate / bullet count / speed); a type at ~0%% with many bullets is "
               "background noise (its pattern never threatens — make it aimed/faster or accept it as filler).\n\n");

    // where enemies die on the screen (Y banded top->bottom). A spike in the TOP bands means
    // the player camps the top / out-ranges the field — most of the arena is wasted.
    int dyTotal = 0, dyTop = 0;
    for (int b = 0; b < DEATH_Y_BANDS; b++) { dyTotal += a->deathYBand[b]; if (b < DEATH_Y_BANDS/2) dyTop += a->deathYBand[b]; }
    fprintf(f, "## Where enemies die (screen Y, top -> bottom)\n\n");
    fprintf(f, "| band (y px) | deaths | |\n|---|---|---|\n");
    for (int b = 0; b < DEATH_Y_BANDS; b++) {
        int lo = b*SCREEN/DEATH_Y_BANDS, hi = (b+1)*SCREEN/DEATH_Y_BANDS;
        int n = a->deathYBand[b];
        int bars = dyTotal ? (n*40/dyTotal) : 0;        // proportional ASCII bar
        char bar[48]; int bi=0; for (; bi<bars && bi<46; bi++) bar[bi]='#'; bar[bi]=0;
        fprintf(f, "| %s%3d-%3d | %d | %s |\n", b==0?"TOP ":(b==DEATH_Y_BANDS-1?"BOT ":"    "), lo, hi, n, bar);
    }
    fprintf(f, "\n- **%.0f%% of kills happen in the top half.** >70%% = the player is camping the top / "
               "out-ranging the wave; consider faster/closing enemies, more aimed fire, or spawns "
               "that flank from the sides/bottom so the whole arena is used.\n\n",
            dyTotal ? 100.0*dyTop/dyTotal : 0);

    // ---- boss ----
    fprintf(f, "## Boss fights\n\n");
    fprintf(f, "- Fights: %d   |   avg TTK: %.0fs   |   damage taken during boss: %.0f   |   head 'clinks' (shielded hits): %d   |   grace timeouts: %d\n\n",
            a->bossFights, AVG(a->bossTTKSum, a->bossFights), a->dmgDuringBoss, a->bossClinks, a->bossGraceTimeouts);

    // ---- offense / weapons / runes ----
    fprintf(f, "## Offense, weapons & runes\n\n");
    fprintf(f, "- Bullets fired: %d   |   accuracy: %.0f%%   |   total damage: %.0f   |   crits: %d (%.0f%% of dmg)\n",
            a->bulletsFired, PCT(a->bulletHits, a->bulletsFired), a->damageDealt, a->critHits, PCT(a->critDamage, a->damageDealt));
    fprintf(f, "- Evolutions formed: %d   |   goo-ignite combos: %d\n", a->evolutionsFormed, a->gooIgnites);
    fprintf(f, "- Status applications: ");
    for (int e = 0; e < EFFECT_COUNT; e++) if (a->effectApplied[e]) fprintf(f, "%s=%d  ", EFFECTS[e].name?EFFECTS[e].name:"?", a->effectApplied[e]);
    fprintf(f, "\n- Runes placed: ");
    for (int r = 0; r < RUNE_COUNT; r++) if (a->runesPlaced[r]) fprintf(f, "%s=%d  ", RUNES[r].name?RUNES[r].name:"?", a->runesPlaced[r]);
    fprintf(f, "\n\n");

    // ---- economy ----
    fprintf(f, "## Economy: XP, gems, pickups\n\n");
    fprintf(f, "- XP earned: %.0f (%.0f/min)   |   level-ups: %d\n", a->xpEarned, AVG(a->xpEarned, mins), a->levelUps);
    fprintf(f, "- Gems dropped: %d, collected: %d (%.0f%%), leaked: %d, pool merges: %d\n",
            a->gemsDropped, a->gemsCollected, PCT(a->gemsCollected,a->gemsDropped), a->gemsLeaked, a->gemMerges);
    fprintf(f, "- Frenzy uptime: %.0f%%   |   board-wipe kills: %d\n", PCT(a->frenzyTime, dur), a->boardWipeKills);
    fprintf(f, "- Pickups (dropped/collected): ");
    for (int k = 0; k < PICKUP_KIND_COUNT; k++) fprintf(f, "%s %d/%d  ", pickup_name(k), a->pickupsDropped[k], a->pickupsCollected[k]);
    fprintf(f, "\n\n");

    // ---- pace / flow ----
    fprintf(f, "## Pace & flow\n\n");
    fprintf(f, "- Idle (no-threat) time: %.1fs (%.0f%%)   |   shake budget: %.0f%%   |   hitflash budget: %.0f%%\n",
            a->idleTime, PCT(a->idleTime, dur), PCT(a->shakeTime, dur), PCT(a->hitFlashTime, dur));
    fprintf(f, "- Time in modals — level-up: %.1fs, pause: %.1fs, codex: %.1fs\n\n",
            a->timeInLevelup, a->timeInPause, a->timeInCodex);

    fclose(f);
    if (outPath && cap) snprintf(outPath, cap, "%s", path);
    #undef PCT
    #undef AVG
}

// ---- high-score table (top 10, persisted to reports/scores.dat) ----
#define SCORES_MAX 10

static const char *scores_path(void) {
    static char p[256];
    snprintf(p, sizeof(p), "reports/scores.dat");
    return p;
}

// Load scores from disk into `out`. Returns count (0..SCORES_MAX).
int analytics_scores_load(ScoreEntry *out) {
    FILE *f = fopen(scores_path(), "rb");
    if (!f) return 0;
    int n = 0;
    while (n < SCORES_MAX && fread(&out[n], sizeof(ScoreEntry), 1, f) == 1) n++;
    fclose(f);
    return n;
}

// Insert this run's score into the table. Returns rank (1-based) or 0 if not top-10.
int analytics_scores_submit(GameWorld *w) {
    mkdir("reports", 0755);
    ScoreEntry entries[SCORES_MAX]; int n = analytics_scores_load(entries);
    Analytics *a = &w->analytics;
    ScoreEntry mine = { w->score, (float)w->runTime, w->stage, w->player.level, "" };
    snprintf(mine.cause, sizeof(mine.cause), "%s", a->causeOfDeath);

    // find insertion point (descending score)
    int pos = n;
    for (int i = 0; i < n; i++) { if (mine.score > entries[i].score) { pos = i; break; } }
    if (pos >= SCORES_MAX) return 0;   // not top-10

    // shift down and insert
    int newn = (n < SCORES_MAX) ? n+1 : SCORES_MAX;
    for (int i = newn-1; i > pos; i--) entries[i] = entries[i-1];
    entries[pos] = mine;

    FILE *f = fopen(scores_path(), "wb");
    if (f) { fwrite(entries, sizeof(ScoreEntry), newn, f); fclose(f); }
    return pos + 1;   // 1-based rank
}
