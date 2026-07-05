// balance.h - tuning knobs, now LOADED FROM assets/balance.toml at runtime (see loader.c).
// Access via the global `BAL`. Old BALANCE #define names are kept as aliases so call sites
// read the same, but they now resolve to runtime fields.
#ifndef ANGERBLAST_BALANCE_H
#define ANGERBLAST_BALANCE_H

typedef struct {
    // player
    float player_radius, shield_max, shield_delay, shield_rate;
    float shield_hit;     // shield drained per enemy hit (>1 makes little blasts hurt more)
    float armor_k;        // armor DR half-point: reduction = armor/(armor+armor_k); lower = stronger
    int   hp;
    float move_speed, pickup_radius, fire_interval, bullet_damage, bullet_speed,
          iframe, accel, friction;
    // barrel-roll dodge: dash speed, active duration, i-frames, cooldown
    float dodge_speed, dodge_time, dodge_iframe, dodge_cooldown;
    // enemy scaling
    float hp_linear, hp_quadratic, speed_per_stage, bullet_base, bullet_per_stage;
    float enemy_speed_mul;   // global multiplier on every enemy's move speed (feel knob)
    // elites get an EXTRA multiplier on top of the normal stage curve, one step per full
    // biome-loop clear (loopCount 0 = first pass, no bonus) -- "tougher every loop around"
    float elite_hp_per_loop, elite_xp_per_loop;
    // director
    int   field_cap_base, field_cap_per_stage, field_cap_max, waves_per_stage_base;
    float wave_interval_base, wave_interval_min_sub, boss_grace_seconds;
    // boss
    float boss_hp_base, boss_hp_per_stage, boss_radius;
    // xp
    double xp_base, xp_exponent;
} Balance;

extern Balance BAL;

// ---- compatibility aliases: existing call sites keep their old names ----
#define PLAYER_RADIUS         (BAL.player_radius)
#define PLAYER_SHIELD_MAX     (BAL.shield_max)
#define PLAYER_SHIELD_DELAY   (BAL.shield_delay)
#define PLAYER_SHIELD_RATE    (BAL.shield_rate)
#define PLAYER_SHIELD_HIT     (BAL.shield_hit)
#define PLAYER_ARMOR_K        (BAL.armor_k)
#define PLAYER_HP             (BAL.hp)
#define PLAYER_MOVE_SPEED     (BAL.move_speed)
#define PLAYER_PICKUP_RADIUS  (BAL.pickup_radius)
#define PLAYER_FIRE_INTERVAL  (BAL.fire_interval)
#define PLAYER_BULLET_DAMAGE  (BAL.bullet_damage)
#define PLAYER_BULLET_SPEED   (BAL.bullet_speed)
#define PLAYER_IFRAME         (BAL.iframe)
#define PLAYER_ACCEL          (BAL.accel)
#define PLAYER_FRICTION       (BAL.friction)
#define DODGE_SPEED           (BAL.dodge_speed)
#define DODGE_TIME            (BAL.dodge_time)
#define DODGE_IFRAME          (BAL.dodge_iframe)
#define DODGE_COOLDOWN        (BAL.dodge_cooldown)

#define ENEMY_HP_LINEAR        (BAL.hp_linear)
#define ENEMY_HP_QUADRATIC     (BAL.hp_quadratic)
#define ENEMY_SPEED_PER_STAGE  (BAL.speed_per_stage)
#define ENEMY_SPEED_MUL        (BAL.enemy_speed_mul)
#define ENEMY_BULLET_BASE      (BAL.bullet_base)
#define ENEMY_BULLET_PER_STAGE (BAL.bullet_per_stage)
#define ELITE_HP_PER_LOOP      (BAL.elite_hp_per_loop)
#define ELITE_XP_PER_LOOP      (BAL.elite_xp_per_loop)

#define FIELD_CAP_BASE        (BAL.field_cap_base)
#define FIELD_CAP_PER_STAGE   (BAL.field_cap_per_stage)
#define FIELD_CAP_MAX         (BAL.field_cap_max)
#define WAVE_INTERVAL_BASE    (BAL.wave_interval_base)
#define WAVE_INTERVAL_MIN_SUB (BAL.wave_interval_min_sub)
#define BOSS_GRACE_SECONDS    (BAL.boss_grace_seconds)
#define WAVES_PER_STAGE_BASE  (BAL.waves_per_stage_base)

#define BOSS_HP_BASE          (BAL.boss_hp_base)
#define BOSS_HP_PER_STAGE     (BAL.boss_hp_per_stage)
#define BOSS_RADIUS           (BAL.boss_radius)

#define XP_BASE               (BAL.xp_base)
#define XP_EXPONENT           (BAL.xp_exponent)

#endif // ANGERBLAST_BALANCE_H

// ---- main-cannon charge shot (stepped, Megaman-style) ----
#define CHARGE_LV1_TIME    0.55f   // step 1: quick, always worth a flick
#define CHARGE_FULL_TIME   1.50f   // step 2: full charge, the event
#define CHARGE_LV1_DMG     4.0f    // x one main-gun shot
#define CHARGE_FULL_DMG    10.0f
#define CHARGE_LV1_SPLASH  55.0f   // blast radius on hit
#define CHARGE_FULL_SPLASH 110.0f
