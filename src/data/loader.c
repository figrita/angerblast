// loader.c - TOML → runtime tables. Built on the vendored tomlc99 parser.
#include "data/loader.h"
#include "data/balance.h"
#include "data/palette.h"
#include "data/runes.h"
#include "data/effects.h"
#include "data/trifectas.h"
#include "data/enemy_types.h"
#include "data/weapons_def.h"
#include "data/evolutions.h"
#include "data/geometry.h"
#include "data/biome.h"
#include "data/emitter.h"
#include "vendor/toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

// ---- string arena: persistent storage for names that structs point at ----
static char g_arena[32768];
static int  g_arenaUsed;
static const char *intern(const char *s) {
    int n = (int)strlen(s) + 1;
    if (g_arenaUsed + n > (int)sizeof(g_arena)) return "?";   // arena full (won't happen)
    char *dst = &g_arena[g_arenaUsed];
    memcpy(dst, s, n); g_arenaUsed += n;
    return dst;
}

Balance BAL;   // the global balance table, filled here

// ---- small parse helpers (fail loud) ----
static bool g_err;
static const char *g_file;

static void perr(const char *key, const char *msg) {
    fprintf(stderr, "DATA ERROR [%s] %s: %s\n", g_file ? g_file : "?", key, msg);
    g_err = true;
}

// optional readers (defined later) — forward-declared so early loaders (balance) can use them
static void opt_f(toml_table_t *t, const char *key, float *out);
static void opt_i(toml_table_t *t, const char *key, int *out);

// read a double from `tab.key`; on miss, leave *out and flag the error
static void rd_f(toml_table_t *tab, const char *key, float *out) {
    toml_datum_t d = toml_double_in(tab, key);
    if (!d.ok) { perr(key, "missing/not a number"); return; }
    *out = (float)d.u.d;
}
static void rd_d(toml_table_t *tab, const char *key, double *out) {
    toml_datum_t d = toml_double_in(tab, key);
    if (!d.ok) { perr(key, "missing/not a number"); return; }
    *out = d.u.d;
}
static void rd_i(toml_table_t *tab, const char *key, int *out) {
    toml_datum_t d = toml_int_in(tab, key);
    if (d.ok) { *out = (int)d.u.i; return; }
    // tolerate an int written as a float (e.g. 3.0)
    toml_datum_t f = toml_double_in(tab, key);
    if (f.ok) { *out = (int)f.u.d; return; }
    perr(key, "missing/not an integer");
}
static void rd_b(toml_table_t *tab, const char *key, bool *out) {
    toml_datum_t d = toml_bool_in(tab, key);
    if (!d.ok) { perr(key, "missing/not a bool"); return; }
    *out = d.u.b;
}
// copy a string field into a fixed buffer; returns false (and flags) on miss
static bool rd_str(toml_table_t *tab, const char *key, char *buf, int cap) {
    toml_datum_t d = toml_string_in(tab, key);
    if (!d.ok) { perr(key, "missing/not a string"); return false; }
    snprintf(buf, cap, "%s", d.u.s); free(d.u.s); return true;
}
// optional string: returns false WITHOUT flagging an error when the key is absent
static bool rd_str_opt(toml_table_t *tab, const char *key, char *buf, int cap) {
    toml_datum_t d = toml_string_in(tab, key);
    if (!d.ok) return false;
    snprintf(buf, cap, "%s", d.u.s); free(d.u.s); return true;
}
// read a colour field (a palette name string) -> Color
static Color rd_color(toml_table_t *tab, const char *key) {
    toml_datum_t d = toml_string_in(tab, key);
    if (!d.ok) { perr(key, "missing/not a colour name"); return (Color){255,255,255,255}; }
    Color c = palette_by_name(d.u.s); free(d.u.s); return c;
}

// open + parse a toml file under the assets dir; caller frees with toml_free()
static toml_table_t *open_toml(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "DATA ERROR: cannot open %s\n", path); g_err = true; return NULL; }
    char errbuf[256];
    toml_table_t *t = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!t) { fprintf(stderr, "DATA ERROR: parse %s: %s\n", path, errbuf); g_err = true; return NULL; }
    g_file = name;
    return t;
}

static toml_table_t *req_table(toml_table_t *root, const char *name) {
    toml_table_t *t = toml_table_in(root, name);
    if (!t) perr(name, "missing [table]");
    return t;
}

// ---- balance.toml ----
static void load_balance(const char *dir) {
    toml_table_t *root = open_toml(dir, "balance.toml");
    if (!root) return;
    toml_table_t *p, *e, *d, *b, *x;
    if ((p = req_table(root, "player"))) {
        rd_f(p,"radius",&BAL.player_radius); rd_f(p,"shield_max",&BAL.shield_max);
        rd_f(p,"shield_delay",&BAL.shield_delay); rd_f(p,"shield_rate",&BAL.shield_rate);
        BAL.shield_hit = 1.0f; opt_f(p,"shield_hit",&BAL.shield_hit);   // optional; default 1
        BAL.armor_k = 100.0f;  opt_f(p,"armor_k",&BAL.armor_k);         // optional; default 100
        rd_i(p,"hp",&BAL.hp); rd_f(p,"move_speed",&BAL.move_speed);
        rd_f(p,"pickup_radius",&BAL.pickup_radius); rd_f(p,"fire_interval",&BAL.fire_interval);
        rd_f(p,"bullet_damage",&BAL.bullet_damage); rd_f(p,"bullet_speed",&BAL.bullet_speed);
        rd_f(p,"iframe",&BAL.iframe); rd_f(p,"accel",&BAL.accel); rd_f(p,"friction",&BAL.friction);
        // dodge: optional with sensible defaults (older balance.toml files still load)
        BAL.dodge_speed=900; BAL.dodge_time=0.28f; BAL.dodge_iframe=0.40f; BAL.dodge_cooldown=0.9f;
        opt_f(p,"dodge_speed",&BAL.dodge_speed); opt_f(p,"dodge_time",&BAL.dodge_time);
        opt_f(p,"dodge_iframe",&BAL.dodge_iframe); opt_f(p,"dodge_cooldown",&BAL.dodge_cooldown);
    }
    if ((e = req_table(root, "enemy_scaling"))) {
        rd_f(e,"hp_linear",&BAL.hp_linear); rd_f(e,"hp_quadratic",&BAL.hp_quadratic);
        rd_f(e,"speed_per_stage",&BAL.speed_per_stage); rd_f(e,"bullet_base",&BAL.bullet_base);
        rd_f(e,"bullet_per_stage",&BAL.bullet_per_stage);
        BAL.enemy_speed_mul = 1.0f; opt_f(e,"enemy_speed_mul",&BAL.enemy_speed_mul);  // optional, default 1
        BAL.elite_hp_per_loop = 0.35f; opt_f(e,"elite_hp_per_loop",&BAL.elite_hp_per_loop);
        BAL.elite_xp_per_loop = 0.25f; opt_f(e,"elite_xp_per_loop",&BAL.elite_xp_per_loop);
    }
    if ((d = req_table(root, "director"))) {
        rd_i(d,"field_cap_base",&BAL.field_cap_base); rd_i(d,"field_cap_per_stage",&BAL.field_cap_per_stage);
        rd_i(d,"field_cap_max",&BAL.field_cap_max); rd_f(d,"wave_interval_base",&BAL.wave_interval_base);
        rd_f(d,"wave_interval_min_sub",&BAL.wave_interval_min_sub);
        rd_f(d,"boss_grace_seconds",&BAL.boss_grace_seconds);
        rd_i(d,"waves_per_stage_base",&BAL.waves_per_stage_base);
    }
    if ((b = req_table(root, "boss"))) {
        rd_f(b,"hp_base",&BAL.boss_hp_base); rd_f(b,"hp_per_stage",&BAL.boss_hp_per_stage);
        rd_f(b,"radius",&BAL.boss_radius);
    }
    if ((x = req_table(root, "xp"))) {
        rd_d(x,"base",&BAL.xp_base); rd_d(x,"exponent",&BAL.xp_exponent);
    }
    toml_free(root);
}

// ---- palette.toml ----
static Color hex_to_color(const char *h) {
    unsigned int r=255,g=255,b=255;
    if (strlen(h) >= 6) sscanf(h, "%2x%2x%2x", &r, &g, &b);
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}
static void load_palette(const char *dir) {
    toml_table_t *root = open_toml(dir, "palette.toml");
    if (!root) return;
    toml_array_t *cols = toml_array_in(root, "colors");
    if (!cols) perr("colors", "missing array");
    else {
        int n = toml_array_nelem(cols);
        if (n > PALETTE_COUNT) n = PALETTE_COUNT;
        for (int i = 0; i < n; i++) {
            toml_datum_t d = toml_string_at(cols, i);
            if (d.ok) { PALETTE[i] = hex_to_color(d.u.s); free(d.u.s); }
        }
    }
    // names array: position in the list = palette index
    toml_array_t *names = toml_array_in(root, "names");
    if (names) {
        int n = toml_array_nelem(names);
        for (int i = 0; i < n; i++) {
            toml_datum_t d = toml_string_at(names, i);
            if (d.ok) { palette_register_name(d.u.s, i); free(d.u.s); }
        }
    }
    toml_free(root);
}

// read a string field and intern it (persistent), returning the interned pointer or NULL
static const char *rd_name(toml_table_t *tab, const char *key) {
    char buf[128];
    if (!rd_str(tab, key, buf, sizeof(buf))) return NULL;
    return intern(buf);
}

// ---- geometry: parse a `shape = [ {op=...}, ... ]` array into a Geometry ----
static int geo_op_id(const char *s) {
    if (strcmp(s,"poly")==0) return GOP_POLY;
    if (strcmp(s,"circle")==0) return GOP_CIRCLE;
    if (strcmp(s,"rect")==0) return GOP_RECT;
    if (strcmp(s,"ring")==0) return GOP_RING;
    if (strcmp(s,"line")==0) return GOP_LINE;
    return -1;
}
// optional float (keeps default if missing) — for layer params that have sensible defaults
static void opt_f(toml_table_t *t, const char *key, float *out) {
    toml_datum_t d = toml_double_in(t, key); if (d.ok) *out = (float)d.u.d;
}
static void opt_i(toml_table_t *t, const char *key, int *out) {
    toml_datum_t d = toml_int_in(t, key); if (d.ok) *out = (int)d.u.i;
}
// resolve a colour token: "body" / "white" / a palette name
static void parse_colorref(const char *s, ColorRef *ref, Color *pal) {
    if (strcmp(s,"body")==0)  { *ref = COLREF_BODY;  return; }
    if (strcmp(s,"white")==0) { *ref = COLREF_WHITE; return; }
    *ref = COLREF_PALETTE; *pal = palette_by_name(s);
}
static void rd_shape(toml_table_t *tab, const char *key, Geometry *geo) {
    geo->count = 0;
    toml_array_t *arr = toml_array_in(tab, key);
    if (!arr) { perr(key, "missing shape array"); return; }
    int n = toml_array_nelem(arr);
    if (n > MAX_SHAPE_LAYERS) n = MAX_SHAPE_LAYERS;
    for (int i = 0; i < n; i++) {
        toml_table_t *L = toml_table_at(arr, i);
        ShapeLayer *s = &geo->layers[geo->count];
        memset(s, 0, sizeof(*s));
        s->r = 1.0f; s->alpha = 1.0f; s->sides = 4; s->w = 2.0f; s->h = 2.0f; s->thick = 0.15f;
        s->colorRef = COLREF_BODY; s->outlineRef = COLREF_WHITE;
        char op[24]; if (!rd_str(L, "op", op, sizeof(op))) continue;
        int oid = geo_op_id(op);
        if (oid < 0) { perr("op", "unknown geometry op"); continue; }
        s->op = oid;
        opt_i(L,"sides",&s->sides);
        opt_f(L,"r",&s->r); opt_f(L,"w",&s->w); opt_f(L,"h",&s->h);
        opt_f(L,"x1",&s->x1); opt_f(L,"y1",&s->y1); opt_f(L,"x2",&s->x2); opt_f(L,"y2",&s->y2);
        opt_f(L,"thick",&s->thick); opt_f(L,"spin",&s->spin);
        opt_f(L,"spin_offset",&s->spinOffset); opt_f(L,"alpha",&s->alpha);
        char col[32];
        if (rd_str_opt(L,"color",col,sizeof(col))) parse_colorref(col,&s->colorRef,&s->paletteCol);
        if (rd_str_opt(L,"outline",col,sizeof(col))) { s->hasOutline=true; parse_colorref(col,&s->outlineRef,&s->paletteCol); }
        geo->count++;
    }
}

// ---- runes.toml ----
static void load_runes(const char *dir) {
    toml_table_t *root = open_toml(dir, "runes.toml");
    if (!root) return;
    toml_array_t *arr = toml_array_in(root, "rune");
    if (!arr) perr("rune", "missing [[rune]] array");
    else {
        int n = toml_array_nelem(arr);
        if (n > RUNE_COUNT) n = RUNE_COUNT;
        for (int i = 0; i < n; i++) {
            toml_table_t *t = toml_table_at(arr, i);
            RUNES[i].name   = rd_name(t, "name");
            RUNES[i].color  = rd_color(t, "color");
            RUNES[i].stat   = rd_name(t, "stat");
            RUNES[i].sprite = NULL;
            char sp[128]; if (rd_str_opt(t, "sprite", sp, sizeof(sp))) RUNES[i].sprite = intern(sp);
        }
    }
    toml_free(root);
}

// ---- effects.toml ----
static StackRule parse_stack(const char *s) {
    if (strcmp(s,"add")==0) return STACK_ADD;
    if (strcmp(s,"max_duration")==0) return STACK_MAX_DURATION;
    return STACK_REFRESH;
}
static void load_effects(const char *dir) {
    toml_table_t *root = open_toml(dir, "effects.toml");
    if (!root) return;
    toml_array_t *arr = toml_array_in(root, "effect");
    if (!arr) perr("effect", "missing [[effect]] array");
    else {
        int n = toml_array_nelem(arr);
        if (n > EFFECT_COUNT) n = EFFECT_COUNT;
        for (int i = 0; i < n; i++) {
            toml_table_t *t = toml_table_at(arr, i);
            EFFECTS[i].name = rd_name(t, "name");
            EFFECTS[i].tint = rd_color(t, "color");
            EFFECTS[i].sprite = NULL;
            char sp[128]; if (rd_str_opt(t, "sprite", sp, sizeof(sp))) EFFECTS[i].sprite = intern(sp);
            rd_f(t, "duration", &EFFECTS[i].defDuration);
            char sr[32]; rd_str(t, "stack", sr, sizeof(sr));
            EFFECTS[i].stackRule = parse_stack(sr);
            int ms = 1; rd_i(t, "max_stacks", &ms); EFFECTS[i].maxStacks = (unsigned char)ms;
            bool b=false; rd_b(t,"dot",&b); EFFECTS[i].isDot=b;
            rd_b(t,"halts_move",&b); EFFECTS[i].haltsMove=b;
            rd_b(t,"halts_fire",&b); EFFECTS[i].haltsFire=b;
        }
    }
    toml_free(root);
}

// ---- trifectas.toml ----
static void load_trifectas(const char *dir) {
    toml_table_t *root = open_toml(dir, "trifectas.toml");
    if (!root) return;
    toml_array_t *arr = toml_array_in(root, "trifecta");
    if (!arr) perr("trifecta", "missing [[trifecta]] array");
    else {
        int n = toml_array_nelem(arr);
        if (n > TRI_COUNT) n = TRI_COUNT;
        for (int i = 0; i < n; i++) {
            toml_table_t *t = toml_table_at(arr, i);
            TRIFECTAS[i].name = rd_name(t, "name");
            TRIFECTAS[i].sprite = NULL;
            char sp[128]; if (rd_str_opt(t, "sprite", sp, sizeof(sp))) TRIFECTAS[i].sprite = intern(sp);
            toml_array_t *c = toml_array_in(t, "counts");
            if (!c) { perr("counts", "missing array"); continue; }
            int cn = toml_array_nelem(c);
            for (int k = 0; k < cn && k < RUNE_COUNT; k++) {
                toml_datum_t d = toml_int_at(c, k);
                TRIFECTAS[i].counts[k] = d.ok ? (int)d.u.i : 0;
            }
        }
    }
    toml_free(root);
}

// read one named key of an inline `move_params`/`fire_params` table into a float slot
static void mp_key(toml_table_t *mt, const char *key, float *slot) {
    if (!mt) return;
    toml_datum_t d = toml_double_in(mt, key);
    if (d.ok) *slot = (float)d.u.d;
    else { toml_datum_t di = toml_int_in(mt, key); if (di.ok) *slot = (float)di.u.i; }
}

// Map a `move_params` inline table onto MoveParams.p[] using each move op's key names.
// The slot meanings mirror the comments in enemies.c enemies_update().
static void rd_move_params(toml_table_t *mt, int op, MoveParams *out) {
    switch (op) {
        case MV_MARCH:
            mp_key(mt,"step_mul",&out->p[0]); mp_key(mt,"edge_margin",&out->p[1]);
            mp_key(mt,"row_drop",&out->p[2]); mp_key(mt,"settle_speed",&out->p[3]); break;
        case MV_DIVE:
            mp_key(mt,"descend_mul",&out->p[0]); mp_key(mt,"hold_time",&out->p[1]);
            mp_key(mt,"dive_mul",&out->p[2]); break;
        case MV_WEAVE:
            mp_key(mt,"phase_rate",&out->p[0]); mp_key(mt,"fall_mul",&out->p[1]);
            mp_key(mt,"wave_amp",&out->p[2]); break;
        case MV_CRAB:
            mp_key(mt,"fall_mul",&out->p[0]); mp_key(mt,"wave_freq",&out->p[1]);
            mp_key(mt,"wave_amp",&out->p[2]); break;
        case MV_SNAKE:
            mp_key(mt,"edge_margin",&out->p[0]); mp_key(mt,"row_drop",&out->p[1]); break;
        case MV_HUNT:
            mp_key(mt,"steer",&out->p[0]); break;
        case MV_HOVER:
            mp_key(mt,"strafe_freq",&out->p[0]); mp_key(mt,"strafe_amp",&out->p[1]); break;
        case MV_ORBIT:
            mp_key(mt,"orbit_time",&out->p[0]); mp_key(mt,"radius",&out->p[1]);
            mp_key(mt,"ang_rate",&out->p[2]);   mp_key(mt,"y_scale",&out->p[3]);
            mp_key(mt,"descend_mul",&out->p[4]); mp_key(mt,"dive_mul",&out->p[5]); break;
        case MV_LUNGE:
            mp_key(mt,"creep_mul",&out->p[0]); mp_key(mt,"trigger_dist",&out->p[1]);
            mp_key(mt,"lunge_mul",&out->p[2]);  mp_key(mt,"lunge_dur",&out->p[3]);
            mp_key(mt,"recover_dur",&out->p[4]); break;
        case MV_SWOOP:
            mp_key(mt,"fall_mul",&out->p[0]); mp_key(mt,"arc_rate",&out->p[1]); mp_key(mt,"arc_amp",&out->p[2]); break;
        case MV_SPIRAL:
            mp_key(mt,"fall_mul",&out->p[0]); mp_key(mt,"spin_rate",&out->p[1]); mp_key(mt,"radius",&out->p[2]); break;
        default: break;   // MV_DRIFT / MV_STRAIGHT take no params
    }
}

// Faithful built-in defaults per move op, so an enemy that omits `move_params` behaves
// exactly as the old hardcoded literals did. TOML `move_params` overlays these.
static void move_defaults(int op, MoveParams *out) {
    memset(out, 0, sizeof(*out));
    switch (op) {
        case MV_MARCH: out->p[0]=1.0f; out->p[1]=40; out->p[2]=26; out->p[3]=6; break;
        case MV_DIVE:  out->p[0]=0.6f; out->p[1]=1.2f; out->p[2]=1.6f; break;
        case MV_WEAVE: out->p[0]=3.0f; out->p[1]=1.0f; out->p[2]=2.7f; break;   // 0.9*3.0
        case MV_CRAB:  out->p[0]=0.6f; out->p[1]=1.5f; out->p[2]=2.5f; break;
        case MV_SNAKE: out->p[0]=30; out->p[1]=30; break;
        case MV_HUNT:  out->p[0]=0.04f; break;
        case MV_HOVER: out->p[0]=0.8f; out->p[1]=2.0f; break;
        case MV_ORBIT: out->p[0]=2.5f; out->p[1]=60; out->p[2]=2.0f; out->p[3]=0.5f; out->p[4]=0.3f; out->p[5]=1.4f; break;
        case MV_LUNGE: out->p[0]=0.5f; out->p[1]=220; out->p[2]=3.0f; out->p[3]=0.45f; out->p[4]=1.0f; break;
        case MV_SWOOP: out->p[0]=0.8f; out->p[1]=2.4f; out->p[2]=2.8f; break;
        case MV_SPIRAL:out->p[0]=0.7f; out->p[1]=3.0f; out->p[2]=70; break;
        default: break;
    }
}

// Overlay `fire_params` keys onto an already-defaulted FireParams.
static void rd_fire_params(toml_table_t *ft, FireParams *fp) {
    if (!ft) return;
    opt_i(ft,"count",&fp->count); opt_i(ft,"count_per_stage",&fp->countPerStage);
    opt_f(ft,"speed_scale",&fp->speedScale); opt_f(ft,"arc",&fp->arc); opt_f(ft,"spin",&fp->spin);
    toml_datum_t ab = toml_bool_in(ft,"aimed"); if (ab.ok) fp->aimed = ab.u.b;
    char c[32]; if (rd_str_opt(ft,"color",c,sizeof(c))) fp->color = palette_by_name(c);
}

// ---- one enemy from a toml table (used by the biome loader) ----
static void enemy_from_table(toml_table_t *t, EnemyType *e) {
    e->name = rd_name(t, "name");
    e->threatId = enemy_threat_id(e->name);   // stable analytics slot (by name, across biomes)
    e->sprite = NULL; e->flavor = NULL;
    char sp[128]; if (rd_str_opt(t, "sprite", sp, sizeof(sp))) e->sprite = intern(sp);
    char fl[192]; if (rd_str_opt(t, "flavor", fl, sizeof(fl))) e->flavor = intern(fl);
    char s[32];
    rd_str(t, "move",  s, sizeof(s)); e->move  = enemy_move_id(s);
    if (e->move  < 0) perr("move",  "unknown move op");
    // move_params: start from the op's faithful defaults, overlay the TOML table
    move_defaults(e->move, &e->movep);
    rd_move_params(toml_table_in(t, "move_params"), e->move, &e->movep);

    char fs[32]; rd_str(t, "fire", fs, sizeof(fs)); e->fire = enemy_fire_id(fs);
    if (e->fire  < 0) perr("fire",  "unknown fire op");
    // fire_params: defaults derived from the fire op NAME, then overlay the TOML table
    enemy_fire_defaults(fs, &e->firep);
    rd_fire_params(toml_table_in(t, "fire_params"), &e->firep);
    // optional composable emitter (overrides the simple radial fire when present)
    e->emitter = -1;
    char em[EMITTER_NAME_MAX];
    if (rd_str_opt(t, "emitter", em, sizeof(em))) {
        e->emitter = emitter_id(em);
        if (e->emitter < 0) perr("emitter", "unknown emitter name");
    }

    rd_shape(t, "shape", &e->geo);   // data-defined geometry
    rd_f(t, "hp", &e->hp); rd_f(t, "speed", &e->speed); rd_f(t, "radius", &e->radius);
    rd_f(t, "fire_interval", &e->fireInterval);
    rd_i(t, "min_stage", &e->minStage);
    rd_f(t, "xp", &e->xp);
    e->color = rd_color(t, "color");
    e->elite = false;
    toml_datum_t eb = toml_bool_in(t, "elite"); if (eb.ok) e->elite = eb.u.b;
    e->deathBurst = 0; opt_i(t, "death_burst", &e->deathBurst);   // melee-danger (optional)
    e->lungeEvery = 0; opt_f(t, "lunge_every", &e->lungeEvery);
}

// formation op names -> id (used by biome directors)
static int formation_id(const char *s) {
    static const char *T[] = {"row","vee","stream","grid","swarm"};
    for (int i=0;i<5;i++) if (strcmp(s,T[i])==0) return i;
    return -1;
}

// ---- formations.toml: shape tunables per op id (geometry stays in spawn.c) ----
static void load_formations(const char *dir) {
    toml_table_t *root = open_toml(dir, "formations.toml");
    if (!root) return;
    toml_array_t *arr = toml_array_in(root, "formation");
    if (!arr) { perr("formation", "missing [[formation]] array"); toml_free(root); return; }
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        char op[24]; if (!rd_str(t, "op", op, sizeof(op))) continue;
        int id = formation_id(op);
        if (id < 0) { perr("op", "unknown formation op"); continue; }
        float *p = FORMATIONS[id].p;
        switch (id) {
            case 0: // row
                mp_key(t,"count",&p[0]); mp_key(t,"count_per_stage",&p[1]); mp_key(t,"count_max",&p[2]);
                mp_key(t,"rows",&p[3]); mp_key(t,"margin",&p[4]); mp_key(t,"row_gap",&p[5]); break;
            case 1: // vee
                mp_key(t,"per",&p[0]); mp_key(t,"per_per_stage",&p[1]); mp_key(t,"per_max",&p[2]);
                mp_key(t,"dx_step",&p[3]); mp_key(t,"y_top_step",&p[4]); mp_key(t,"y_anchor_step",&p[5]); break;
            case 2: // stream
                mp_key(t,"lanes",&p[0]); mp_key(t,"lanes_div",&p[1]); mp_key(t,"lanes_max",&p[2]);
                mp_key(t,"count",&p[3]); mp_key(t,"count_max",&p[4]); mp_key(t,"y_step",&p[5]); break;
            case 3: // grid
                mp_key(t,"cols",&p[0]); mp_key(t,"cols_div",&p[1]); mp_key(t,"cols_max",&p[2]);
                mp_key(t,"rows",&p[3]); mp_key(t,"margin",&p[4]); mp_key(t,"row_gap",&p[5]); break;
            default: // swarm
                mp_key(t,"count",&p[0]); mp_key(t,"count_per_stage",&p[1]); mp_key(t,"count_max",&p[2]); break;
        }
    }
    toml_free(root);
}

// ---- emitters.toml: the composable danmaku graph ----
static int child_mode_id(const char *s) {
    if (strcmp(s,"timer")==0) return CHILD_ON_TIMER;
    if (strcmp(s,"death")==0) return CHILD_ON_DEATH;
    return CHILD_NONE;
}
static void load_emitters(const char *dir) {
    toml_table_t *root = open_toml(dir, "emitters.toml");
    if (!root) return;   // emitters are optional
    toml_array_t *arr = toml_array_in(root, "emitter");
    if (!arr) { toml_free(root); return; }
    int n = toml_array_nelem(arr);
    if (n > MAX_EMITTERS) n = MAX_EMITTERS;
    // pass 1: names + scalar params (so child references can resolve by name afterward)
    char childName[MAX_EMITTERS][EMITTER_NAME_MAX];
    for (int i = 0; i < n; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        Emitter *e = &EMITTERS[i];
        memset(e, 0, sizeof(*e));
        e->speedScale = 1.0f; e->arc = 2*PI; e->radius = 5; e->child = -1; e->color = (Color){255,255,255,255};
        childName[i][0] = 0;
        char nm[EMITTER_NAME_MAX];
        if (rd_str(t,"name",nm,sizeof(nm))) snprintf(e->name, sizeof(e->name), "%s", nm);
        opt_i(t,"count",&e->count); opt_i(t,"count_per_stage",&e->countPerStage);
        opt_f(t,"speed_scale",&e->speedScale); opt_f(t,"arc",&e->arc); opt_f(t,"spin",&e->spin);
        opt_f(t,"radius",&e->radius);
        toml_datum_t ab = toml_bool_in(t,"aimed"); if (ab.ok) e->aimed = ab.u.b;
        char col[32]; if (rd_str_opt(t,"color",col,sizeof(col))) e->color = palette_by_name(col);
        // child reference (resolved in pass 2)
        rd_str_opt(t,"child",childName[i],sizeof(childName[i]));
        char cm[16]; if (rd_str_opt(t,"child_mode",cm,sizeof(cm))) e->childMode = child_mode_id(cm);
        opt_f(t,"child_interval",&e->childInterval); opt_f(t,"child_speed",&e->childSpeed);
        if (e->count < 1) e->count = 1;
    }
    NUM_EMITTERS = n;
    // pass 2: resolve child names -> indices (fail loud on an unknown reference)
    for (int i = 0; i < n; i++) {
        if (childName[i][0]) {
            int cid = emitter_id(childName[i]);
            if (cid < 0) { g_file = "emitters.toml"; perr(childName[i], "unknown child emitter"); }
            EMITTERS[i].child = cid;
        }
    }
    toml_free(root);
}

// ---- weapons.toml ----
static void load_weapons(const char *dir) {
    toml_table_t *root = open_toml(dir, "weapons.toml");
    if (!root) return;
    toml_array_t *arr = toml_array_in(root, "weapon");
    if (!arr) { perr("weapon", "missing [[weapon]] array"); toml_free(root); return; }
    int n = toml_array_nelem(arr);
    if (n > WP_COUNT) n = WP_COUNT;
    for (int i = 0; i < n; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        WeaponDef *wd = &WEAPONS_DEF[i];
        wd->name = rd_name(t, "name");
        wd->sprite = NULL;
        char sp[128]; if (rd_str_opt(t, "sprite", sp, sizeof(sp))) wd->sprite = intern(sp);
        rd_f(t, "base_interval", &wd->baseInterval);
        rd_f(t, "base_damage",   &wd->baseDamage);
        wd->color = rd_color(t, "color");

        // emit op + params (the firing behaviour)
        char es[32]; rd_str(t, "emit", es, sizeof(es)); wd->emit = weapon_emit_id(es);
        if (wd->emit < 0) perr("emit", "unknown emit op");
        EmitParams *ep = &wd->ep; memset(ep, 0, sizeof(*ep));
        ep->speedScale = 1.0f;   // sensible default (no scaling)
        toml_table_t *et = toml_table_in(t, "emit_params");
        if (et) {
            opt_f(et,"speed_scale",&ep->speedScale); opt_f(et,"radius",&ep->radius);
            opt_f(et,"spread",&ep->spread); opt_i(et,"count",&ep->count);
            opt_i(et,"pierce",&ep->pierce); opt_i(et,"evo_bonus",&ep->evoBonus);
            toml_datum_t hb = toml_bool_in(et,"homing"); if (hb.ok) ep->homing = hb.u.b;
            opt_f(et,"base_radius",&ep->baseRadius); opt_f(et,"per_level",&ep->perLevel);
            opt_f(et,"per_multi",&ep->perMulti);     opt_f(et,"evo_radius",&ep->evoRadius);
            opt_i(et,"targets",&ep->targets);        opt_f(et,"range",&ep->range);
            toml_array_t *da = toml_array_in(et, "dirs");
            if (da) { int dn = toml_array_nelem(da);
                for (int k=0;k<dn && k<4;k++){ toml_datum_t d=toml_double_at(da,k);
                    if (d.ok) ep->dirs[ep->dirCount++] = (float)d.u.d;
                    else { toml_datum_t di=toml_int_at(da,k); if(di.ok) ep->dirs[ep->dirCount++]=(float)di.u.i; } } }
        }
    }
    toml_free(root);
}

// ---- evolutions.toml ----
static int spec_id(const char *s) {
    static const char *T[] = {"none","explode","beam","swarm","rapid","pierceall","burn",
        "shotgun","rail","storm","voidpull","prism","chaos"};
    for (int i=0;i<13;i++) if (strcmp(s,T[i])==0) return i;
    return 0;
}
static int onkill_id(const char *s) {
    static const char *T[] = {"none","magnet","chain","shield","bounty","frag","reignite"};
    for (int i=0;i<7;i++) if (strcmp(s,T[i])==0) return i;
    return 0;
}
static int effect_id(const char *s) {  // returns EffectId or -1 for "none"
    static const char *T[] = {"slow","burn","poison","freeze","shock","weaken","mark","goo"};
    if (strcmp(s,"none")==0) return -1;
    for (int i=0;i<8;i++) if (strcmp(s,T[i])==0) return i;
    return -1;
}
// trifecta column order in evolutions.toml [[row]] arrays + [mods.*] tables
static const char *TRI_KEYS[TRI_COUNT] = {"inferno","stream","swarm","blitz","lance","napalm",
    "scatter","railgun","tempest","void","prism","chaos"};

static void load_evolutions(const char *dir) {
    toml_table_t *root = open_toml(dir, "evolutions.toml");
    if (!root) return;
    // 1) the 12 trifecta mod presets
    EvoMods mods[TRI_COUNT];
    toml_table_t *modsTab = req_table(root, "mods");
    if (modsTab) for (int c = 0; c < TRI_COUNT; c++) {
        toml_table_t *m = toml_table_in(modsTab, TRI_KEYS[c]);
        if (!m) { perr(TRI_KEYS[c], "missing [mods.*]"); continue; }
        EvoMods *o = &mods[c];
        rd_f(m,"damage_mul",&o->damageMul); rd_f(m,"rate_mul",&o->rateMul); rd_f(m,"size_mul",&o->sizeMul);
        rd_i(m,"multi_add",&o->multiAdd); rd_i(m,"pierce_add",&o->pierceAdd);
        char s[24];
        rd_str(m,"special",s,sizeof(s)); o->special = spec_id(s);
        rd_str(m,"on_hit_effect",s,sizeof(s)); o->onHitEffect = effect_id(s);
        rd_f(m,"on_hit_mag",&o->onHitEffectMag);
        rd_str(m,"on_kill",s,sizeof(s)); o->onKill = onkill_id(s);
        rd_str(m,"aura_effect",s,sizeof(s)); o->auraEffect = effect_id(s);
    }
    // 2) the name/desc grid: one [[row]] per weapon, 12 evolutions each (trifecta order)
    toml_array_t *rows = toml_array_in(root, "row");
    if (!rows) perr("row", "missing [[row]] array");
    else {
        int nr = toml_array_nelem(rows);
        if (nr > WP_COUNT) nr = WP_COUNT;
        for (int wp = 0; wp < nr; wp++) {
            toml_table_t *r = toml_table_at(rows, wp);
            toml_array_t *evo = toml_array_in(r, "evo");
            if (!evo) { perr("evo", "missing array in row"); continue; }
            int ne = toml_array_nelem(evo);
            if (ne > TRI_COUNT) ne = TRI_COUNT;
            for (int c = 0; c < ne; c++) {
                toml_table_t *cell = toml_table_at(evo, c);
                EVOLUTIONS[wp][c].name = rd_name(cell, "name");
                EVOLUTIONS[wp][c].desc = rd_name(cell, "desc");
                EVOLUTIONS[wp][c].mods = mods[c];
                // per-cell overrides: every weapon x trifecta cell can differ from the
                // column preset, so each of the 156 evolutions is mechanically unique
                EvoMods *o = &EVOLUTIONS[wp][c].mods;
                opt_f(cell,"damage_mul",&o->damageMul); opt_f(cell,"rate_mul",&o->rateMul);
                opt_f(cell,"size_mul",&o->sizeMul);
                opt_i(cell,"multi_add",&o->multiAdd); opt_i(cell,"pierce_add",&o->pierceAdd);
                char s[24];
                if (rd_str_opt(cell,"special",s,sizeof(s)))        o->special = spec_id(s);
                if (rd_str_opt(cell,"on_hit_effect",s,sizeof(s))) o->onHitEffect = effect_id(s);
                opt_f(cell,"on_hit_mag",&o->onHitEffectMag);
                if (rd_str_opt(cell,"on_kill",s,sizeof(s)))       o->onKill = onkill_id(s);
                if (rd_str_opt(cell,"aura_effect",s,sizeof(s)))   o->auraEffect = effect_id(s);
            }
        }
    }
    toml_free(root);
}

// ---- one biome folder: biome.toml + enemies/*.toml ----
static int cmpstr(const void *a, const void *b) { return strcmp(*(const char**)a, *(const char**)b); }
static char *dupstr(const char *s) { int n=(int)strlen(s)+1; char *d=malloc(n); if(d) memcpy(d,s,n); return d; }

static void load_biome(const char *biomeDir, Biome *bm) {
    // slug = the folder name; generated art (backgrounds/) is keyed by it
    const char *slash = strrchr(biomeDir, '/');
    bm->slug = intern(slash ? slash + 1 : biomeDir);
    // biome.toml (theme, director, boss)
    char path[800];
    snprintf(path, sizeof(path), "%s/biome.toml", biomeDir);
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "DATA ERROR: missing %s\n", path); g_err = true; return; }
    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf)); fclose(fp);
    if (!root) { fprintf(stderr, "DATA ERROR: parse %s: %s\n", path, errbuf); g_err = true; return; }
    g_file = path;

    bm->name = rd_name(root, "name");
    toml_table_t *theme = toml_table_in(root, "theme");
    bm->bulletColor = palette_by_name("pinkred");   // readable defaults if theme omits them
    bm->gemColor    = palette_by_name("gold");
    if (theme) {
        bm->background = rd_color(theme, "background");
        char tc[32];
        if (rd_str_opt(theme, "bullet_color", tc, sizeof(tc))) bm->bulletColor = palette_by_name(tc);
        if (rd_str_opt(theme, "gem_color",    tc, sizeof(tc))) bm->gemColor    = palette_by_name(tc);
        toml_array_t *sc = toml_array_in(theme, "star_colors");
        for (int i = 0; i < 3 && sc && i < toml_array_nelem(sc); i++) {
            toml_datum_t d = toml_string_at(sc, i);
            if (d.ok) { bm->stars[i] = palette_by_name(d.u.s); free(d.u.s); }
        }
    }
    toml_table_t *dir = toml_table_in(root, "director");
    if (dir) {
        BiomeDirector *bd = &bm->director;
        rd_i(dir,"field_cap_base",&bd->fieldCapBase); rd_i(dir,"field_cap_per_stage",&bd->fieldCapPerStage);
        rd_i(dir,"field_cap_max",&bd->fieldCapMax); rd_i(dir,"waves_to_boss",&bd->wavesToBoss);
        rd_f(dir,"wave_interval_base",&bd->waveIntervalBase);
        toml_array_t *fm = toml_array_in(dir, "formations");
        if (fm) { int fn = toml_array_nelem(fm);
            for (int i=0;i<fn && i<MAX_FORMATIONS;i++){ toml_datum_t d=toml_string_at(fm,i);
                if (d.ok){ int id=formation_id(d.u.s); free(d.u.s); if(id>=0) bd->formations[bd->formationCount++]=id; } } }
    }
    toml_table_t *boss = toml_table_in(root, "boss");
    if (boss) {
        BossDef *b = &bm->boss;
        b->name = rd_name(boss, "name");
        b->sprite = NULL;
        char bsp[128]; if (rd_str_opt(boss, "sprite", bsp, sizeof(bsp))) b->sprite = intern(bsp);
        rd_f(boss,"hp",&b->hp); rd_f(boss,"radius",&b->radius);
        toml_array_t *ph = toml_array_in(boss, "phases");
        if (ph) { int pn = toml_array_nelem(ph);
            for (int i=0;i<pn && i<MAX_BOSS_PHASES;i++){ toml_table_t *pt=toml_table_at(ph,i);
                BossPhase *bp=&b->phases[b->phaseCount++];
                rd_f(pt,"hp_above",&bp->hpAbove); rd_i(pt,"count",&bp->count);
                char fs[24]; bool hasFire = rd_str_opt(pt,"fire",fs,sizeof(fs));
                bp->fireOp = hasFire ? enemy_fire_id(fs) : -1;
                char ps[24]; bp->plusFlower = (rd_str_opt(pt,"plus",ps,sizeof(ps)) && strcmp(ps,"flower")==0);
                // Build the phase's danmaku FireParams from the named op + count, reusing the
                // Step-3 emit_radial. Boss bullets are a touch faster than enemy bullets, so
                // we fold the old absolute speeds (150 / 120) into speedScale over bullet_base.
                enemy_fire_defaults(hasFire ? fs : "aimed", &bp->mainFp);
                bp->mainFp.count = bp->count;
                bp->mainFp.speedScale = 150.0f / (BAL.bullet_base > 0 ? BAL.bullet_base : 110.0f);
                bp->mainFp.color = palette_by_name("pinkred");
                // A boss "aimed" pattern with many bullets needs a spread arc (the old boss
                // fanned by 0.14 rad per bullet); give a finite fan when one wasn't implied.
                if (bp->mainFp.count > 1 && bp->mainFp.arc <= 0.0001f)
                    bp->mainFp.arc = 0.14f * (bp->mainFp.count - 1);
                // allow an explicit fire_params override on the phase (optional)
                rd_fire_params(toml_table_in(pt, "fire_params"), &bp->mainFp);
                // the flower overlay: a rotating hotpink full ring (was 6+phase*2 petals)
                enemy_fire_defaults("flower", &bp->flowerFp);
                bp->flowerFp.speedScale = 120.0f / (BAL.bullet_base > 0 ? BAL.bullet_base : 110.0f);
                bp->flowerFp.color = palette_by_name("hotpink");
            } }
        // multi-part bosses: [[boss.part]] head + hands. Each part has its own hp + danmaku.
        toml_array_t *pa = toml_array_in(boss, "part");
        if (pa) { int pn = toml_array_nelem(pa);
            for (int i=0;i<pn && i<MAX_BOSS_PARTS;i++){ toml_table_t *pt=toml_table_at(pa,i);
                BossPartDef *bp=&b->parts[b->partCount++];
                memset(bp, 0, sizeof(*bp));
                bp->radius = 28; bp->interval = 1.0f; bp->fireOp = -1; bp->emitter = -1;
                bp->color = palette_by_name("pinkred");
                char psp[128]; if (rd_str_opt(pt, "sprite", psp, sizeof(psp))) bp->sprite = intern(psp);
                opt_f(pt,"off_x",&bp->offX); opt_f(pt,"off_y",&bp->offY);
                rd_f(pt,"hp",&bp->hp); opt_f(pt,"radius",&bp->radius);
                opt_f(pt,"interval",&bp->interval); opt_i(pt,"count",&bp->count);
                toml_datum_t hb = toml_bool_in(pt,"head"); if (hb.ok) bp->isHead = hb.u.b;
                char ccol[32]; if (rd_str_opt(pt,"color",ccol,sizeof(ccol))) bp->color = palette_by_name(ccol);
                char fs[24]; bool hasFire = rd_str_opt(pt,"fire",fs,sizeof(fs));
                bp->fireOp = hasFire ? enemy_fire_id(fs) : -1;
                enemy_fire_defaults(hasFire ? fs : "aimed", &bp->fp);
                if (bp->count > 0) bp->fp.count = bp->count;
                bp->fp.speedScale = 140.0f / (BAL.bullet_base > 0 ? BAL.bullet_base : 110.0f);
                bp->fp.color = bp->color;
                if (bp->fp.count > 1 && bp->fp.arc <= 0.0001f) bp->fp.arc = 0.16f * (bp->fp.count - 1);
                rd_fire_params(toml_table_in(pt, "fire_params"), &bp->fp);
                char em[EMITTER_NAME_MAX];
                if (rd_str_opt(pt,"emitter",em,sizeof(em))) {
                    bp->emitter = emitter_id(em);
                    if (bp->emitter < 0) perr("emitter","unknown emitter name (boss part)");
                }
                // optional per-part silhouette (same format as enemy shape=); missing => circle
                if (toml_array_in(pt, "shape")) rd_shape(pt, "shape", &bp->geo);
                // optional keyframed motion: anim = [{t=,x=,y=,rot=,scale=,tween=},...]
                toml_array_t *an = toml_array_in(pt, "anim");
                if (an) { int kn = toml_array_nelem(an);
                    if (kn > MAX_BOSS_KEYFRAMES) kn = MAX_BOSS_KEYFRAMES;
                    for (int k = 0; k < kn; k++) {
                        toml_table_t *kt = toml_table_at(an, k);
                        BossKeyframe *kf = &bp->anim.frames[bp->anim.frameCount++];
                        kf->offX = bp->offX; kf->offY = bp->offY; kf->scale = 1.0f;
                        opt_f(kt,"t",&kf->t); opt_f(kt,"x",&kf->offX); opt_f(kt,"y",&kf->offY);
                        opt_f(kt,"rot",&kf->rotation); opt_f(kt,"scale",&kf->scale);
                        char tw[16];
                        if (rd_str_opt(kt,"tween",tw,sizeof(tw))) {
                            int ti = tween_id(tw);
                            if (ti < 0) { perr("tween","unknown tween type (boss part anim)"); ti = TWEEN_LINEAR; }
                            kf->tween = (TweenType)ti;
                        }
                    }
                    toml_datum_t lb = toml_bool_in(pt,"anim_loops"); if (lb.ok) bp->anim.loops = lb.u.b;
                    opt_f(pt,"anim_duration",&bp->anim.duration);
                }
            } }
    }
    toml_free(root);

    // enemies/ folder: load every *.toml, sorted by filename
    char edir[800]; snprintf(edir, sizeof(edir), "%s/enemies", biomeDir);
    DIR *d = opendir(edir);
    if (!d) { fprintf(stderr, "DATA ERROR: missing %s/\n", edir); g_err = true; return; }
    char *files[MAX_BIOME_ROSTER]; int nf = 0;
    struct dirent *de;
    while ((de = readdir(d)) && nf < MAX_BIOME_ROSTER) {
        const char *nm = de->d_name; int len = (int)strlen(nm);
        if (len > 5 && strcmp(nm+len-5, ".toml") == 0) files[nf++] = dupstr(nm);
    }
    closedir(d);
    qsort(files, nf, sizeof(char*), cmpstr);
    for (int i = 0; i < nf; i++) {
        char fp2[900]; snprintf(fp2, sizeof(fp2), "%s/%s", edir, files[i]);
        FILE *f = fopen(fp2, "r");
        if (f) { toml_table_t *et = toml_parse_file(f, errbuf, sizeof(errbuf)); fclose(f);
            if (et) { g_file = files[i]; enemy_from_table(et, &bm->roster[bm->rosterCount++]); toml_free(et); }
            else { fprintf(stderr, "DATA ERROR: parse %s: %s\n", fp2, errbuf); g_err = true; } }
        free(files[i]);
    }
}

// ---- scan assets/biomes/ for biome folders (alphabetical) ----
static void load_biomes(const char *assetsDir) {
    char biomesDir[512]; snprintf(biomesDir, sizeof(biomesDir), "%s/biomes", assetsDir);
    DIR *d = opendir(biomesDir);
    if (!d) { fprintf(stderr, "DATA ERROR: missing %s/\n", biomesDir); g_err = true; return; }
    char *dirs[MAX_BIOMES]; int nd = 0;
    struct dirent *de;
    while ((de = readdir(d)) && nd < MAX_BIOMES) {
        if (de->d_name[0] == '.') continue;     // skip ., .., hidden
        dirs[nd++] = dupstr(de->d_name);
    }
    closedir(d);
    qsort(dirs, nd, sizeof(char*), cmpstr);
    NUM_BIOMES = 0;
    for (int i = 0; i < nd; i++) {
        char bdir[700]; snprintf(bdir, sizeof(bdir), "%s/%s", biomesDir, dirs[i]);
        // only treat dirs that contain a biome.toml as biomes
        char probe[800]; snprintf(probe, sizeof(probe), "%s/biome.toml", bdir);
        FILE *p = fopen(probe, "r");
        if (p) { fclose(p); load_biome(bdir, &BIOMES[NUM_BIOMES++]); }
        free(dirs[i]);
    }
    if (NUM_BIOMES == 0) { fprintf(stderr, "DATA ERROR: no biomes found in %s/\n", biomesDir); g_err = true; }
}

bool loader_load_all(const char *assetsDir) {
    g_err = false; g_file = NULL; g_arenaUsed = 0;
    NUM_BIOMES = 0;
    for (int i = 0; i < MAX_BIOMES; i++) { BIOMES[i].rosterCount = 0; BIOMES[i].director.formationCount = 0; BIOMES[i].boss.phaseCount = 0; BIOMES[i].boss.partCount = 0; }
    memset(FORMATIONS, 0, sizeof(FORMATIONS));   // re-zero before (re)load
    NUM_EMITTERS = 0;
    enemy_threat_reset();   // rebuild the global enemy-name registry from scratch
    load_palette(assetsDir);   // first: other files reference colours by name
    load_balance(assetsDir);
    load_runes(assetsDir);
    load_effects(assetsDir);
    load_trifectas(assetsDir);
    load_weapons(assetsDir);
    load_evolutions(assetsDir);
    load_formations(assetsDir);
    load_emitters(assetsDir);  // before biomes: enemies/bosses can reference emitters by name
    load_biomes(assetsDir);    // last: biomes reference colours
    return !g_err;
}
