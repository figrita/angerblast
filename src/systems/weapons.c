// weapons.c - the socketed auto-weapon loadout. Each held weapon ticks on its own timer,
// reading its effective stats from its level + the runes in its sockets (+ its evolution).
#include "systems/weapons.h"
#include "platform/audio.h"
#include "data/palette.h"
#include "systems/bullets.h"
#include "systems/enemies.h"
#include "systems/fx.h"
#include "systems/puddles.h"
#include "platform/input.h"
#include "systems/pilot.h"
#include "data/weapons_def.h"
#include "data/runes.h"
#include "data/trifectas.h"
#include "core/settings.h"
#include "data/balance.h"
#include <math.h>

// ---- effective stats: fold weapon level + socketed runes ----
// ---- main-cannon charge shot (Star Fox / Megaman-style) --------------------
// Stepped curve: LV1 at CHARGE_LV1_TIME (4x + small splash), LV2 at
// CHARGE_FULL_TIME (10x + big splash). Charging SILENCES the main gun
// — hold fire to charge, or leave auto-fire off and the cannon charges by
// itself (SETTINGS.autoCharge). Release/tap lets it rip; the stream resumes.

static int charge_level(float c) {
    return c >= CHARGE_FULL_TIME ? 2 : c >= CHARGE_LV1_TIME ? 1 : 0;
}

// sticky lock-on: keep a still-valid lock; else grab the target nearest to a
// point AHEAD of the ship (biases the column you're actually shooting into)
static void lock_validate_or_acquire(GameWorld *w) {
    Player *p = &w->player;
    if (p->lockOn) {
        if (p->lockIsBoss) {
            if (w->boss.active && (p->lockIdx < 0 ||
                (p->lockIdx < w->boss.partCount && w->boss.parts[p->lockIdx].alive))) return;
        } else if (p->lockIdx >= 0 && w->enemies.items[p->lockIdx].active) return;
        p->lockOn = false;
    }
    Vector2 ahead = { p->pos.x, p->pos.y - 220 };
    Vector2 tp; bool isBoss; int idx;
    if (target_nearest(w, ahead, 1e9f, -1, false, &tp, &isBoss, &idx)) {
        p->lockOn = true; p->lockIsBoss = isBoss; p->lockIdx = idx;
        audio_play_pitched(&w->audio, SFX_UI_MOVE, 1.5f, 0.7f);   // lock blip
    }
}

static void maingun_fire_charged(GameWorld *w, int level) {
    Player *p = &w->player;
    float mul = 1.0f;
    for (int i = 0; i < w->loadout.count; i++)
        if (w->loadout.items[i].type == WP_MAINGUN) {
            WeaponStats st = weapon_stats(&w->loadout.items[i]);
            mul = st.damageMul;
            break;
        }
    float dmg = WEAPONS_DEF[WP_MAINGUN].baseDamage * mul * p->globalDamageMul
              * (level >= 2 ? CHARGE_FULL_DMG : CHARGE_LV1_DMG);
    w->bulletStyle   = BSTYLE_BLOB;
    w->bulletOnHit   = HIT_SPLASH;
    w->bulletSplashR = level >= 2 ? CHARGE_FULL_SPLASH : CHARGE_LV1_SPLASH;
    Vector2 origin = { p->pos.x, p->pos.y - p->radius - 12 };
    // same speed class as a normal shot — the payoff is size, splash + seeking
    int bi = bullets_add(w, origin, (Vector2){ 0, -p->bulletSpeed * 0.8f },
                level >= 2 ? 15.0f : 9.0f, dmg,
                PALETTE[level >= 2 ? PC_GOLD : PC_AQUABRT], true, WK_MAIN,
                0, p->lockOn);
    if (bi >= 0) {
        Bullet *b = &w->bullets.items[bi];
        b->chargeShot = true;
        b->life = 1.4f;           // short fuse: it detonates (splash) where it expires
        if (p->lockOn) { b->seek = true; b->seekIsBoss = p->lockIsBoss; b->seekIdx = p->lockIdx; }
    }
    p->chargeShotIdx = bi;
    p->lockOn = false;
    w->bulletStyle = BSTYLE_DOT; w->bulletOnHit = HIT_NONE; w->bulletSplashR = 0;
    w->muzzleFlash = 0.09f;
    w->shakeAmount = fmaxf(w->shakeAmount, level >= 2 ? 10 : 5);
    audio_play_pitched(&w->audio, SFX_SHOOT_BIG, level >= 2 ? 0.72f : 1.0f, 1.0f);
    input_rumble(level >= 2 ? 0.6f : 0.3f, level >= 2 ? 0.9f : 0.5f, 0.18f);
}

static void maingun_charge_update(GameWorld *w, float dt) {
    Player *p = &w->player;
    w->maingunTapFire = false;
    bool held = pilot_fire_down(w);
    if (pilot_autofire_toggle(w)) {
        p->autofire = !p->autofire;
        audio_play(&w->audio, SFX_UI_SELECT);
        // flipping auto-fire ON releases the idle-built charge right away — unless
        // the button is held (that's a deliberate charge, keep it). Sub-step charge
        // is dropped so it can't keep the resumed stream silenced.
        if (p->autofire && !held) {
            if (charge_level(p->charge) >= 1) p->chargePending = true;   // fires ASAP below
            else { p->charge = 0; p->chargePulse = 0; }
        }
    }
    bool pressed  =  held && !p->fireHeldPrev;
    bool released = !held &&  p->fireHeldPrev;
    p->fireHeldPrev = held;

    int before = charge_level(p->charge);
    // charge while the button is held, or continuously whenever auto-fire is off
    // (the SETTINGS.autoCharge option disables only the hands-off charging)
    bool charging = held || (!p->autofire && SETTINGS.autoCharge);
    if (charging) {
        p->charge = fminf(p->charge + dt, CHARGE_FULL_TIME);
        int lvl = charge_level(p->charge);
        if (lvl > before)   // step crossings get their own thunk, deeper at full
            audio_play(&w->audio, lvl >= 2 ? SFX_CHARGE_FULL : SFX_CHARGE_STEP);
        p->chargePulse -= dt;
        if (p->chargePulse <= 0 && p->charge < CHARGE_FULL_TIME) {
            float u = p->charge / CHARGE_FULL_TIME;   // pulse deepens as it fills
            audio_play_pitched(&w->audio, SFX_CHARGE, 1.25f - 0.65f * u, 0.35f + 0.35f * u);
            p->chargePulse = 0.13f;
        }
    }
    // lock-on exists from LV1 up (that's when a release actually seeks)
    if (charge_level(p->charge) >= 1) lock_validate_or_acquire(w);
    else p->lockOn = false;
    // only ONE charged shot may live: a release while it flies keeps your charge
    // and fires the instant the slot frees up
    bool shotLive = p->chargeShotIdx >= 0
        && w->bullets.items[p->chargeShotIdx].active
        && w->bullets.items[p->chargeShotIdx].chargeShot;
    if (!shotLive) p->chargeShotIdx = -1;
    if (p->chargePending && !shotLive && charge_level(p->charge) >= 1) {
        maingun_fire_charged(w, charge_level(p->charge));
        p->charge = 0; p->chargePulse = 0; p->chargePending = false;
        return;
    }
    if (!shotLive) p->chargePending = false;
    // release of a held charge fires it; with auto-fire off, the tap that
    // interrupts an idle-built charge fires it instead (else it's a normal volley)
    if (released) {
        if (charge_level(p->charge) >= 1) {
            if (shotLive) { p->chargePending = true; return; }   // hold the charge
            maingun_fire_charged(w, charge_level(p->charge));
        }
        p->charge = 0; p->chargePulse = 0;
    } else if (pressed && !p->autofire) {
        if (charge_level(p->charge) >= 1) {
            if (shotLive) { p->chargePending = true; return; }   // hold the charge
            maingun_fire_charged(w, charge_level(p->charge));
            p->charge = 0; p->chargePulse = 0;
        } else {
            w->maingunTapFire = true;
            p->charge = 0;   // the tap volley restarts any sub-step charge
        }
    }
}

WeaponStats weapon_stats(const Weapon *wp) {
    WeaponStats s = { .damageMul = 1, .rateMul = 1, .velMul = 1, .sizeMul = 1, .pierce = 0, .multi = 0,
                      .onHitEffect = -1, .onKill = ONKILL_NONE, .auraEffect = -1 };
    // weapon level: each level adds a flat damage bump + slight rate
    s.damageMul += 0.5f * (wp->level - 1);
    s.rateMul   *= powf(0.92f, (float)(wp->level - 1));   // higher level fires a bit faster
    for (int i = 0; i < SOCKETS; i++) {
        switch (wp->runes[i]) {
            case RUNE_RED:    s.damageMul += 0.25f; break;
            case RUNE_BLUE:   s.velMul    += 0.25f; break;
            case RUNE_GREEN:  s.multi     += 1;     break;
            case RUNE_YELLOW: s.rateMul   *= 0.80f; break;  // lower interval = faster
            case RUNE_PURPLE: s.pierce    += 1;     break;
            default: break;
        }
    }
    // evolution: apply the weapon's unique per-trifecta mods from the evolution table
    if (wp->evolution >= 0) {
        const EvoMods *m = &EVOLUTIONS[wp->type][wp->evolution].mods;
        s.damageMul *= m->damageMul;
        s.rateMul   *= m->rateMul;
        s.sizeMul   *= m->sizeMul;
        s.multi     += m->multiAdd;
        s.pierce    += m->pierceAdd;
        s.special    = m->special;
        s.onHitEffect = m->onHitEffect;
        s.onHitEffectMag = m->onHitEffectMag;
        s.onKill     = m->onKill;
        s.auraEffect = m->auraEffect;
        s.evolved    = true;
    }
    return s;
}

// Emit a directional shot, reshaped by the weapon's evolution `special`. This is what
// makes each evolution *fire differently* (pattern) and *look different* (style/colour),
// not just hit harder. `baseA` is the aim angle; `homing` is the weapon's base homing.
static void emit_special(GameWorld *w, Vector2 origin, float baseA, float speed,
                         float radius, float dmg, int pierce, bool homing,
                         Color baseColor, int special, int multi) {
    // defaults
    w->bulletStyle = BSTYLE_DOT;
    w->bulletOnHit = (special==SPEC_EXPLODE)?HIT_EXPLODE : (special==SPEC_BURN)?HIT_BURN : HIT_NONE;
    Color col = baseColor;

    // Every pattern fires at least `1 + multi` projectiles so an evolution NEVER reduces
    // your shot count — your accumulated upgrades (multi runes + Spread) always carry over
    // and add on top of the evolution's own projectiles.
    int base = 1 + multi;
    switch (special) {
        case SPEC_BEAM: {            // long, fast, thin piercing lances
            w->bulletStyle = BSTYLE_BEAM; col = PALETTE[PC_PALECYAN];
            int n = base; Vector2 perp = { -sinf(baseA), cosf(baseA) };
            for (int k=0;k<n;k++){ float off=(n>1?(k-(n-1)/2.0f)*8:0);
                Vector2 o={origin.x+perp.x*off,origin.y+perp.y*off};
                bullets_add(w, o,(Vector2){cosf(baseA)*speed*1.8f,sinf(baseA)*speed*1.8f},
                            radius*0.7f, dmg, col, true, WK_MAIN, pierce<99?pierce+5:pierce, false); }
            break;
        }
        case SPEC_RAIL: {            // huge, fat, slow piercing slugs
            w->bulletStyle = BSTYLE_SLUG; col = PALETTE[PC_AQUALT];
            int n = base; Vector2 perp = { -sinf(baseA), cosf(baseA) };
            for (int k=0;k<n;k++){ float off=(n>1?(k-(n-1)/2.0f)*22:0);
                Vector2 o={origin.x+perp.x*off,origin.y+perp.y*off};
                bullets_add(w, o,(Vector2){cosf(baseA)*speed,sinf(baseA)*speed},
                            radius*2.4f, dmg, col, true, WK_MAIN, 999, false); }
            w->shakeAmount = fmaxf(w->shakeAmount, 6);
            break;
        }
        case SPEC_SHOTGUN: {         // wide cone of pellets (a burst PER projectile owned)
            int pellets = 6 + multi*3;
            for (int k=0;k<pellets;k++){ float a=baseA + frandr(w, -0.5f,0.5f);
                float sp = speed*frandr(w, 0.8f,1.2f);
                bullets_add(w, origin,(Vector2){cosf(a)*sp,sinf(a)*sp}, radius, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
        case SPEC_STORM: {           // tight rapid burst with jitter
            int n = 3 + multi;
            for (int k=0;k<n;k++){ float a=baseA + frandr(w, -0.14f,0.14f);
                bullets_add(w, origin,(Vector2){cosf(a)*speed*frandr(w, 0.9f,1.15f),sinf(a)*speed}, radius*0.8f, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
        case SPEC_SWARM: {           // many parallel shots across a width
            int n = 4 + multi; Vector2 perp = { -sinf(baseA), cosf(baseA) };
            for (int k=0;k<n;k++){ float off=(k-(n-1)/2.0f)*10;
                Vector2 o = { origin.x+perp.x*off, origin.y+perp.y*off };
                bullets_add(w, o,(Vector2){cosf(baseA)*speed,sinf(baseA)*speed}, radius, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
        case SPEC_PIERCEALL: {       // piercing lances (one per projectile owned)
            w->bulletStyle = BSTYLE_BEAM;
            int n = base; Vector2 perp = { -sinf(baseA), cosf(baseA) };
            for (int k=0;k<n;k++){ float off=(n>1?(k-(n-1)/2.0f)*10:0);
                Vector2 o={origin.x+perp.x*off,origin.y+perp.y*off};
                bullets_add(w, o,(Vector2){cosf(baseA)*speed*1.3f,sinf(baseA)*speed*1.3f}, radius, dmg, col, true, WK_MAIN, 999, false); }
            break;
        }
        case SPEC_VOIDPULL: {        // big slow heavy diamonds, deep pierce
            w->bulletStyle = BSTYLE_DIAMOND; col = PALETTE[PC_VIOLET];
            int n = base; Vector2 perp = { -sinf(baseA), cosf(baseA) };
            for (int k=0;k<n;k++){ float off=(n>1?(k-(n-1)/2.0f)*16:0);
                Vector2 o={origin.x+perp.x*off,origin.y+perp.y*off};
                bullets_add(w, o,(Vector2){cosf(baseA)*speed*0.8f,sinf(baseA)*speed*0.8f}, radius*1.8f, dmg, col, true, WK_MAIN, pierce+4, false); }
            break;
        }
        case SPEC_PRISM: {           // fanned rainbow shots: always 3 + your extra projectiles
            Color cols[3]={PALETTE[PC_CORAL],PALETTE[PC_SEAGREEN],PALETTE[PC_AZURE]};
            int n = 3 + multi;
            for (int k=0;k<n;k++){ float off=(n>1?((float)k/(n-1)-0.5f):0)*0.36f;
                bullets_add(w, origin,(Vector2){cosf(baseA+off)*speed,sinf(baseA+off)*speed}, radius, dmg, cols[k%3], true, WK_MAIN, pierce, homing); }
            break;
        }
        case SPEC_CHAOS: {           // erratic homing star-scatter
            w->bulletStyle = BSTYLE_STAR; col = PALETTE[PC_HOTPINK];
            int n = 3 + multi;
            for (int k=0;k<n;k++){ float a=baseA + frandr(w, -1.0f,1.0f);
                bullets_add(w, origin,(Vector2){cosf(a)*speed,sinf(a)*speed}, radius, dmg, col, true, WK_MAIN, pierce, true); }
            break;
        }
        case SPEC_EXPLODE: {         // detonating shots (one per projectile owned)
            w->bulletStyle = BSTYLE_RING; col = PALETTE[PC_PEACH];
            int n = base;
            for (int k=0;k<n;k++){ float off=(n>1?((float)k/(n-1)-0.5f):0)*0.30f;
                bullets_add(w, origin,(Vector2){cosf(baseA+off)*speed,sinf(baseA+off)*speed}, radius*1.2f, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
        case SPEC_BURN: {            // fire shots (one per projectile owned)
            col = PALETTE[PC_PEACH];
            int n = base;
            for (int k=0;k<n;k++){ float off=(n>1?((float)k/(n-1)-0.5f):0)*0.30f;
                bullets_add(w, origin,(Vector2){cosf(baseA+off)*speed,sinf(baseA+off)*speed}, radius, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
        default: {                   // SPEC_NONE / SPEC_RAPID: plain shot(s)
            int n = base;
            for (int k=0;k<n;k++){ float off=(n>1?((float)k/(n-1)-0.5f):0)*0.30f;
                bullets_add(w, origin,(Vector2){cosf(baseA+off)*speed,sinf(baseA+off)*speed}, radius, dmg, col, true, WK_MAIN, pierce, homing); }
            break;
        }
    }
    w->bulletStyle = BSTYLE_DOT; w->bulletOnHit = HIT_NONE;   // reset
}

// A fan of `shots` projectiles centred on `baseA`, spread over `spreadArc` radians TOTAL
// (caller computes the total: fixed for MAINGUN, per-step×multi for aimed/autoaim). The
// generic "straight/aimed/autoaim" body (the non-evolved path).
static void emit_fan(GameWorld *w, Vector2 origin, float baseA, int shots, float spreadArc,
                     float speed, float radius, float dmg, Color col, int wk, int pierce, bool homing) {
    if (shots < 1) shots = 1;
    for (int k = 0; k < shots; k++) {
        float off = (shots == 1) ? 0 : ((float)k/(shots-1) - 0.5f) * spreadArc;
        float a = baseA + off;
        bullets_add(w, origin, (Vector2){cosf(a)*speed, sinf(a)*speed}, radius, dmg, col, true, wk, pierce, homing);
    }
}

// ---- weapon firing, dispatched on def->emit (not the WeaponId). Each emitter reads its
// numbers from def->ep; the evolution `special` still reshapes the projectile emitters via
// emit_special (which owns the "never reduces shot count" rule). ----
// Fire `wp` from `origin`. The player's loadout passes origin; a drone passes its own pos.
// Global mults (damage/rate) come from the player and apply to drones too.
static void fire_weapon(GameWorld *w, Weapon *wp, Vector2 origin, float dt) {
    Player *p = &w->player;
    const WeaponDef *def = &WEAPONS_DEF[wp->type];
    const EmitParams *ep = &def->ep;
    WeaponStats st = weapon_stats(wp);
    // Frenzy pickup: temporary overdrive (faster fire + more damage) while the timer runs.
    float frenzyRate = (p->frenzyTimer > 0) ? 0.55f : 1.0f;   // lower interval = faster
    float frenzyDmg  = (p->frenzyTimer > 0) ? 1.5f  : 1.0f;
    float bspeed = p->bulletSpeed * st.velMul;
    float interval = def->baseInterval * st.rateMul * p->globalRateMul * frenzyRate;
    float dmg = def->baseDamage * st.damageMul * p->globalDamageMul * frenzyDmg;
    int multi = st.multi;

    // evolved projectiles carry an on-hit effect for the duration of this fire call
    w->bulletOnHit = (st.special == SPEC_EXPLODE) ? HIT_EXPLODE
                   : (st.special == SPEC_BURN)    ? HIT_BURN : HIT_NONE;
    // status / on-kill / crit stamps carried onto this weapon's bullets
    w->bulletEffect     = st.onHitEffect;
    w->bulletEffectMag  = st.onHitEffectMag;
    w->bulletMagnetKill = (st.onKill == ONKILL_MAGNET);
    w->bulletOnKill     = st.onKill;
    // Aegis: only ~35% of shots gain point-defense (and each pops just one bullet, then spends
    // it). Defensive flavour without a permanent bullet-eating forcefield from one passive pick.
    w->bulletDestroysBullets = p->aegis && (frand(w) < 0.35f);

    int forcedPierce = (ep->pierce > 0) ? ep->pierce : st.pierce;   // ION etc force deep pierce

    switch (def->emit) {
        case EMIT_STRAIGHT: {   // MAINGUN, TAILGUN, ION — a fan along a fixed direction
            wp->timer -= dt;
            // the main gun goes quiet while a charge is building (the charge IS the
            // shot); with auto-fire off it additionally only fires on a tap
            if (wp->type == WP_MAINGUN && !w->maingunTapFire
                && (p->charge > 0 || !p->autofire)) {
                if (wp->timer < 0) wp->timer = 0;
                return;
            }
            if (wp->timer > 0) return;
            wp->timer = interval;
            float speed = bspeed * (ep->speedScale > 0 ? ep->speedScale : 1.0f);
            float baseA = (wp->type == WP_TAILGUN) ? PI/2 : -PI/2;   // tail gun fires down
            int wk = WK_MAIN;
            // MAINGUN's Spread passive + base-pierce expansion (only the main gun has these)
            int extra = (wp->type == WP_MAINGUN) ? p->spreadLevel : 0;
            int basePierce = forcedPierce + ((wp->type == WP_MAINGUN && p->piercing) ? p->pierceCount : 0);
            int mMulti = multi + extra;
            if (st.special != SPEC_NONE) {
                emit_special(w, origin, baseA, speed, ep->radius, dmg, basePierce, false, def->color, st.special, mMulti);
            } else {
                int shots = 1 + mMulti;
                // TAILGUN/ION fan by an x-offset rather than an angle (vertical guns): keep that look
                if (wp->type == WP_TAILGUN || wp->type == WP_ION) {
                    float ox_step = (wp->type == WP_ION) ? 16 : 8;
                    float vy = (wp->type == WP_TAILGUN) ? speed : -speed;
                    for (int m = 0; m < shots; m++) { float ox = (shots>1 ? (m-(shots-1)/2.0f)*ox_step : 0);
                        bullets_add(w, (Vector2){origin.x+ox,origin.y},(Vector2){0,vy}, ep->radius, dmg, def->color, true, wk, basePierce, false); }
                } else {
                    emit_fan(w, origin, baseA, shots, ep->spread, speed, ep->radius, dmg, def->color, wk, basePierce, false);
                }
            }
            if (wp->type == WP_MAINGUN) audio_play(&w->audio, SFX_SHOOT);
            if (wp->type == WP_MAINGUN) w->muzzleFlash = 0.06f;   // flash + 2px visual recoil
            if (wp->type == WP_ION) { fx_spawn_particles(w, origin, 8, PALETTE[PC_SKY], 120); w->shakeAmount = fmaxf(w->shakeAmount, 4); }
            return;
        }
        case EMIT_FIXED_DIRS: {  // SIDEGUN — one (×multi jitter) shot along each fixed angle
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            float speed = bspeed * (ep->speedScale > 0 ? ep->speedScale : 1.0f);
            for (int di = 0; di < ep->dirCount; di++) {
                float a = ep->dirs[di];
                if (st.special != SPEC_NONE) {
                    emit_special(w, origin, a, speed, ep->radius, dmg, st.pierce, false, def->color, st.special, multi);
                } else for (int m = 0; m <= multi; m++) {
                    // mostly-horizontal shots with a vertical jitter per extra projectile
                    // (the old side-gun spread). cos(0)=+1 (right), cos(PI)=-1 (left).
                    float vy = -speed*0.5f + m*0.12f*speed*(frand(w)>.5f?1:-1);
                    bullets_add(w, origin,(Vector2){ cosf(a)*speed, vy },
                                ep->radius+st.sizeMul, dmg, def->color, true, WK_SIDE, st.pierce, false);
                }
            }
            return;
        }
        case EMIT_AIMED: {       // AIMED — fan along the input aim direction
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            Vector2 d = pilot_aim(w, origin); float baseA = atan2f(d.y,d.x);
            if (st.special != SPEC_NONE) {
                emit_special(w, origin, baseA, bspeed, ep->radius, dmg, st.pierce, false, def->color, st.special, multi);
            } else emit_fan(w, origin, baseA, 1+multi, ep->spread*multi, bspeed, ep->radius, dmg, def->color, WK_MAIN, st.pierce, false);
            return;
        }
        case EMIT_AUTOAIM: {     // AUTOAIM — fan toward nearest target (incl. boss)
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            Vector2 tp; bool isBoss; int ei;
            if (target_nearest(w, origin, ep->range>0?ep->range:700, -1, false, &tp, &isBoss, &ei)) {
                Vector2 d = Vector2Normalize(Vector2Subtract(tp, origin)); float baseA=atan2f(d.y,d.x);
                float speed = bspeed * (ep->speedScale > 0 ? ep->speedScale : 1.0f);
                if (st.special != SPEC_NONE) {
                    emit_special(w, origin, baseA, speed, ep->radius, dmg, st.pierce, false, def->color, st.special, multi);
                } else emit_fan(w, origin, baseA, 1+multi, ep->spread*multi, speed, ep->radius, dmg, def->color, WK_MAIN, st.pierce, false);
            }
            return;
        }
        case EMIT_HOMING_VOLLEY: {  // HOMING (scatter ring), MISSILE (forward cone)
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            int count = 1 + multi + (st.evolved ? ep->evoBonus : 0);
            float speed = (ep->speedScale > 0 ? ep->speedScale : 1.0f);   // absolute speeds here
            for (int k = 0; k < count; k++) {
                float a = (ep->spread >= 2*PI - 0.01f) ? frand(w)*2*PI       // full scatter (HOMING)
                                                       : -PI/2 + frandr(w, -ep->spread*0.5f, ep->spread*0.5f);  // forward cone (MISSILE)
                bullets_add(w, origin,(Vector2){cosf(a)*speed,sinf(a)*speed}, ep->radius, dmg, def->color, true, WK_HOMING, st.pierce, true);
            }
            return;
        }
        case EMIT_CHAIN: {       // LIGHTNING — chains that hop nearest targets, fx bolts. Hits the boss.
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            int eff = st.evolved ? st.onHitEffect : -1;
            // multi (green) = number of SEPARATE chains; pierce (purple) = extra HOPS per chain.
            int chains = 1 + multi;
            int hops   = ep->targets + st.pierce;
            float range = ep->range>0 ? ep->range : 420;
            for (int c = 0; c < chains; c++) {
                Vector2 from = origin; int skip = -1; bool skipBoss = false;
                for (int t = 0; t < hops; t++) {
                    Vector2 tp; bool isBoss; int ei;
                    if (!target_nearest(w, from, range, skip, skipBoss, &tp, &isBoss, &ei)) break;
                    fx_spawn_bolt(w, from, tp, PALETTE[PC_PALECYAN]);
                    fx_spawn_particles(w, tp, 3, PALETTE[PC_PALECYAN], 120);
                    target_damage(w, isBoss, ei, dmg, eff, st.onHitEffectMag);
                    from = tp;
                    if (isBoss) skipBoss = true; else skip = ei;   // within a chain, don't re-hit
                }
            }
            return;
        }
        case EMIT_AURA: {        // AURA — pulse AoE in a growing radius
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            float r = ep->baseRadius + wp->level*ep->perLevel + multi*ep->perMulti + (st.evolved ? ep->evoRadius : 0);
            int aeff = st.evolved ? (st.auraEffect>=0?st.auraEffect:st.onHitEffect) : -1;
            enemies_damage_near_fx(w, origin, r, dmg, aeff, 0.6f, st.onHitEffectMag);
            return;
        }
        case EMIT_ORBITAL: {     // ORBITAL — continuous rotating contact balls (no timer)
            float ang = (float)w->runTime * 3.0f;
            int balls = ep->targets + multi; if (balls > MAX_ORBITALS) balls = MAX_ORBITALS;
            float orbitR = ep->baseRadius + wp->level*ep->perLevel + (st.evolved ? ep->evoRadius : 0);
            float tickDmg = dmg * dt * 4.0f;
            int aeff = st.evolved ? (st.auraEffect>=0?st.auraEffect:st.onHitEffect) : -1;
            for (int b = 0; b < balls; b++) { float a = ang + (float)b/balls*2*PI;
                Vector2 op = { origin.x+cosf(a)*orbitR, origin.y+sinf(a)*orbitR };
                enemies_damage_near_fx(w, op, 12 + st.pierce*2, tickDmg, aeff, 0.5f, st.onHitEffectMag); }
            return;
        }
        case EMIT_CONE: {        // FLAME — scattered short-range AoE points
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            int eff = st.evolved ? st.onHitEffect : -1;
            float reach = ep->baseRadius + wp->level*ep->perLevel + (st.evolved ? ep->evoRadius : 0);
            int count = (ep->count>0?ep->count:5) + multi;
            for (int s = 0; s < count; s++) { float a = -PI/2 + frandr(w, -ep->spread*0.5f, ep->spread*0.5f); float d = frandr(w, 20, reach);
                Vector2 fp = { origin.x+cosf(a)*d, origin.y+sinf(a)*d };
                enemies_damage_near_fx(w, fp, 18, dmg, eff, 0, st.onHitEffectMag);
                fx_spawn_particles(w, fp, 1, (irand(w, 0,1)?PALETTE[PC_PEACH]:PALETTE[PC_GOLD]), 40); }
            return;
        }
        case EMIT_LOB: {         // GOO — slow wads that coat EFFECT_GOO on hit
            wp->timer -= dt; if (wp->timer > 0) return; wp->timer = interval;
            w->bulletStyle  = BSTYLE_BLOB;
            w->bulletEffect = EFFECT_GOO;
            float speed = bspeed * (ep->speedScale > 0 ? ep->speedScale : 1.0f);
            int wads = 1 + multi;
            float impact = dmg * 0.5f;
            for (int k = 0; k < wads; k++) {
                float off = (wads==1) ? 0 : ((float)k/(wads-1)-0.5f) * 0.28f;
                float a = -PI/2 + off;
                bullets_add(w, origin,(Vector2){ cosf(a)*speed, sinf(a)*speed },
                            ep->radius, impact, PALETTE[PC_LIME], true, WK_MAIN, st.pierce, false);
            }
            return;
        }
        default: return;
    }
}

// reset all per-weapon bullet stamps so nothing leaks to the next weapon / drone
static void weapon_stamps_clear(GameWorld *w) {
    w->bulletOnHit = HIT_NONE; w->bulletStyle = BSTYLE_DOT;
    w->bulletEffect = -1; w->bulletEffectMag = 0;
    w->bulletMagnetKill = false; w->bulletOnKill = 0; w->bulletBounce = 0; w->bulletCrit = false;
    w->bulletDestroysBullets = false; w->bulletSplashR = 0;
}

// Fire one weapon from `origin` (used by drones). Clears stamps after, like weapons_update.
void weapons_fire_one(GameWorld *w, Weapon *wp, Vector2 origin, float dt) {
    if (wp->type < 0) return;
    fire_weapon(w, wp, origin, dt);
    weapon_stamps_clear(w);
}

void weapons_update(GameWorld *w, float dt) {
    maingun_charge_update(w, dt);
    for (int i = 0; i < w->loadout.count; i++) {
        Weapon *wp = &w->loadout.items[i];
        if (wp->type < 0) continue;
        fire_weapon(w, wp, w->player.pos, dt);
        weapon_stamps_clear(w);
    }
}
