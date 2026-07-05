// render.c - all drawing, read-only over GameWorld.
#include "platform/render.h"
#include "platform/input.h"          // pad_on (for control hints)
#include "systems/upgrades_sys.h"    // card model, rune placement
#include "systems/weapons.h"         // weapon_stats / WeaponStats
#include "systems/drones.h"          // drone names + AI for cards/encyclopedia
#include "systems/pickups.h"         // pickup names/colours for render + encyclopedia
#include "data/enemy_types.h"
#include "data/upgrades.h"
#include "data/runes.h"
#include "data/balance.h"            // PLAYER_ARMOR_K (armor DR% readout)
#include "data/weapons_def.h"
#include "data/trifectas.h"
#include "data/effects.h"
#include "data/palette.h"
#include "data/biome.h"
#include "data/geometry.h"           // geometry_draw (enemy portraits)
#include "platform/sprites.h"        // generated pixel-art sprites (geometry is the fallback)
#include "core/settings.h"           // player options (fps overlay, freeze-frames)
#include "systems/analytics.h"       // ScoreEntry, analytics_scores_load
#include "rlgl.h"                    // matrix stack (rotated boss-part geometry)
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>


// ---- pixel-perfect text ----
// raylib's default font is a 10px bitmap: any non-multiple-of-10 size scales
// fractionally and strokes go randomly thick/thin. Snap every size to an integer
// multiple so glyphs stay crisp; measure with the SAME snap so centering holds.
static int px_snap(int size) { int m = (size + 5) / 10; if (m < 1) m = 1; return m * 10; }
static void px_draw(const char *text, int x, int y, int size, Color c) {
    int fs = px_snap(size);
    DrawTextEx(GetFontDefault(), text, (Vector2){ (float)x, (float)y }, (float)fs, (float)fs/10.0f, c);
}
static int px_measure(const char *text, int size) {
    int fs = px_snap(size);
    return (int)MeasureTextEx(GetFontDefault(), text, (float)fs, (float)fs/10.0f).x;
}
#define DrawText    px_draw
#define MeasureText px_measure

void render_toggle_fps(void) { SETTINGS.showFps = !SETTINGS.showFps; settings_save(); }

// Draw text word-wrapped within maxW pixels. Returns the total height used (line count * lineH).
static int draw_text_wrapped(const char *text, int x, int y, int fs, int maxW, Color c) {
    char buf[256]; strncpy(buf, text, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    int lineH = fs + 4, lines = 0, wx = x;
    char *start = buf, *p = buf, *lastSpace = NULL;
    while (*p) {
        if (*p == ' ') lastSpace = p;
        *p = *p; // no-op; just scanning
        char saved = *(p+1); *(p+1) = '\0';
        int w = MeasureText(start, fs);
        *(p+1) = saved;
        if (w > maxW && lastSpace) {
            *lastSpace = '\0';
            DrawText(start, wx, y + lines*lineH, fs, c);
            lines++;
            start = lastSpace + 1;
            lastSpace = NULL;
        }
        p++;
    }
    DrawText(start, wx, y + lines*lineH, fs, c);   // final fragment
    return (lines+1) * lineH;
}

// ---- device-aware button prompts -------------------------------------------
// Hint strings embed {tokens}; each resolves to a ui/ button icon for whichever
// device was touched last (keyboard / PS / Xbox — input_prompt_device). Icons
// are 14px tall, integer-scaled 2x under large text.
typedef struct { const char *tok; const char *kb, *ps, *xb; } PromptIcon;
static const PromptIcon PROMPTS[] = {
    { "move",    "sprites/ui/kb_wasd.png",   "sprites/ui/stick.png",       "sprites/ui/stick.png"    },
    { "dodge",   "sprites/ui/kb_shift.png",  "sprites/ui/ps_r1.png",       "sprites/ui/xb_rb.png"    },
    { "fire",    "sprites/ui/kb_space.png",  "sprites/ui/ps_cross.png",    "sprites/ui/xb_a.png"     },
    { "auto",    "sprites/ui/kb_t.png",      "sprites/ui/ps_triangle.png", "sprites/ui/xb_y.png"     },
    { "pause",   "sprites/ui/kb_esc.png",    "sprites/ui/ps_options.png",  "sprites/ui/xb_menu.png"  },
    { "codex",   "sprites/ui/kb_c.png",      "sprites/ui/ps_square.png",   "sprites/ui/xb_x.png"     },
    { "confirm", "sprites/ui/kb_space.png",  "sprites/ui/ps_cross.png",    "sprites/ui/xb_a.png"     },
    { "menu",    "sprites/ui/kb_arrows.png", "sprites/ui/ps_dpad.png",     "sprites/ui/xb_dpad.png"  },
};
static const char *prompt_sprite(const char *tok, size_t len) {
    for (size_t i = 0; i < sizeof(PROMPTS)/sizeof(PROMPTS[0]); i++)
        if (strlen(PROMPTS[i].tok) == len && strncmp(PROMPTS[i].tok, tok, len) == 0) {
            switch (input_prompt_device()) {
                case PROMPT_PS:   return PROMPTS[i].ps;
                case PROMPT_XBOX: return PROMPTS[i].xb;
                default:          return PROMPTS[i].kb;
            }
        }
    return NULL;
}
// walk a hint string: draw (or just measure when draw=false); returns width
static int hint_walk(const char *s, int x, int y, int fs, Color c, bool draw) {
    int startX = x;
    char buf[128];
    while (*s) {
        if (*s == '{') {
            const char *end = strchr(s, '}');
            const char *spr = end ? prompt_sprite(s + 1, (size_t)(end - s - 1)) : NULL;
            const Texture2D *t = spr ? sprite_get(spr) : NULL;
            if (t) {
                int k = fs >= 26 ? 2 : 1;
                if (draw) DrawTextureEx(*t, (Vector2){ (float)x, (float)(y + (fs - t->height*k)/2) }, 0, (float)k, WHITE);
                x += t->width*k + 3;
                s = end + 1; continue;
            }
        }
        size_t n = strcspn(s, "{");
        if (n == 0) n = 1;                    // lone '{' with no matching token
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, s, n); buf[n] = '\0';
        if (draw) DrawText(buf, x, y, fs, c);
        x += MeasureText(buf, fs);
        s += n;
    }
    return x - startX;
}
static void draw_hint(const char *s, int cx, int y, int fs, Color c) {
    int w2 = hint_walk(s, 0, 0, fs, c, false);
    hint_walk(s, cx - w2/2, y, fs, c, true);
}

// A cut crystal: pointed top & bottom, faceted body, lit right side, drifting shine.
static void draw_gem(Vector2 p, float baseR, Color c, float t, int seed) {
    float pulse = 1.0f + sinf(t*6 + seed)*0.12f;
    float w = baseR * 0.7f * pulse;   // half-width
    float h = baseR * 1.15f * pulse;  // half-height (taller than wide -> pointed)
    float sh = h * 0.42f;             // shoulder height (where the body is widest)

    Vector2 top = { p.x,     p.y - h };
    Vector2 bot = { p.x,     p.y + h };
    Vector2 ul  = { p.x - w, p.y - sh };
    Vector2 ur  = { p.x + w, p.y - sh };
    Vector2 ll  = { p.x - w, p.y + sh };
    Vector2 lr  = { p.x + w, p.y + sh };

    Color lit  = c;                                                   // right-facing facets
    Color dark = (Color){ (unsigned char)(c.r*0.55f), (unsigned char)(c.g*0.55f),
                          (unsigned char)(c.b*0.55f), c.a };          // shadowed left facets
    Color edge = (Color){ (unsigned char)fminf(255, c.r+80),
                          (unsigned char)fminf(255, c.g+80),
                          (unsigned char)fminf(255, c.b+80), c.a };
    Vector2 ctr = p;

    // Facets fan out from the centre; left column shadowed, right column lit.
    // raylib culls clockwise tris, so each is given CCW winding explicitly.
    DrawTriangle(ctr, top, ul, dark);   // top-left facet
    DrawTriangle(ctr, ul, ll, dark);    // mid-left facet
    DrawTriangle(ctr, ll, bot, dark);   // bottom-left facet
    DrawTriangle(ctr, bot, lr, lit);    // bottom-right facet
    DrawTriangle(ctr, lr, ur, lit);     // mid-right facet
    DrawTriangle(ctr, ur, top, lit);    // top-right facet

    // crisp outline of the silhouette
    DrawLineEx(top, ur, 1.4f, edge);
    DrawLineEx(ur, lr, 1.4f, edge);
    DrawLineEx(lr, bot, 1.4f, edge);
    DrawLineEx(bot, ll, 1.4f, edge);
    DrawLineEx(ll, ul, 1.4f, edge);
    DrawLineEx(ul, top, 1.4f, edge);

    // shininess: a bright core + a small drifting specular highlight
    DrawCircleV(p, baseR*0.18f, Fade(WHITE, 0.5f));
    float gleam = (sinf(t*3 + seed)*0.5f + 0.5f);                     // 0..1 sweep
    Vector2 spec = { p.x + (w*0.35f), p.y - h*0.4f + gleam*sh };
    DrawCircleV(spec, baseR*0.16f, Fade(WHITE, 0.85f*gleam + 0.15f));
}

static void draw_enemy_shape(GameWorld *w, Enemy *e) {
    const EnemyType *T = &ETYPES[e->type];
    Vector2 p = e->pos; float r = e->radius; Color c = e->color;
    float rt = (float)w->runTime;
    // blend in the tint of any active status effects (body color for geometry,
    // a white-based tint for sprites so the art keeps its own colors when clean)
    Color spriteTint = WHITE;
    for (int id = 0; id < EFFECT_COUNT; id++) {
        if (e->effTimer[id] <= 0) continue;
        Color t = EFFECTS[id].tint;
        c.r = (unsigned char)((c.r + t.r) / 2);
        c.g = (unsigned char)((c.g + t.g) / 2);
        c.b = (unsigned char)((c.b + t.b) / 2);
        spriteTint.r = (unsigned char)((spriteTint.r + t.r) / 2);
        spriteTint.g = (unsigned char)((spriteTint.g + t.g) / 2);
        spriteTint.b = (unsigned char)((spriteTint.b + t.b) / 2);
    }
    // elites (minibosses) get a pulsing aura + an outer ring so they read as a threat.
    // Looped elites (eliteTier > 1, i.e. spawned after a full biome-loop clear) burn hotter
    // and wear an extra outer ring so a returning-loop miniboss reads as visibly tougher,
    // not just a recolor -- the tier is baked in at spawn (enemies.c), not derived from the
    // CURRENT loop, so an elite spawned before a loop-over still shows its own tier.
    if (e->eliteTier > 0) {
        int tier = e->eliteTier;                        // 1 = first pass, 2+ = looped
        float pulse = 0.5f + 0.3f*sinf(rt*4 + p.x);
        Color auraColor = tier > 1 ? PALETTE[PC_CORAL] : PALETTE[PC_GOLD];
        float auraBoost = 1.0f + (tier - 1) * 0.18f;     // bigger aura per loop tier
        DrawCircleV(p, r*1.5f*auraBoost, Fade(c, 0.18f*pulse));
        DrawCircleLines((int)p.x,(int)p.y, r*1.4f*auraBoost, Fade(auraColor, 0.5f));
        DrawCircleLines((int)p.x,(int)p.y, r*1.6f*auraBoost, Fade(auraColor, 0.25f));
        for (int ring = 1; ring < tier; ring++) {        // one extra ring per loop past the first
            float rr = r*(1.8f + ring*0.25f)*auraBoost;
            float ringPulse = 0.5f + 0.3f*sinf(rt*3 - ring*1.3f + p.y);
            DrawCircleLines((int)p.x,(int)p.y, rr, Fade(PALETTE[PC_CORAL], 0.35f*ringPulse));
        }
    }
    // death-burst enemies wear a slow-spinning spiked outline: "I explode, keep back"
    if (T->deathBurst > 0) {
        int spikes = T->deathBurst < 8 ? T->deathBurst : 8;
        float pulse = 0.35f + 0.2f*sinf(rt*6 + p.y);
        DrawPolyLines(p, spikes, r*1.25f, rt*40, Fade(PALETTE[PC_PEACH], pulse));
    }
    // pre-fire telegraph: a ring converges on the enemy as its shot charges, then the
    // release ring (spawned by enemy_fire) kicks outward — windup and release both read.
    if (T->fireInterval >= 0.8f && (T->fire != FR_NONE || T->emitter >= 0)
        && e->fireTimer < 0.35f && e->fireTimer > 0 && p.y > 0 && p.y < SCREEN) {
        float tt = 1.0f - e->fireTimer/0.35f;                  // 0 -> 1 as the shot nears
        float rr = r*2.1f - (r*1.5f)*tt;
        DrawCircleLines((int)p.x, (int)p.y, rr, Fade(PALETTE[PC_WHITE], 0.25f + 0.55f*tt));
        DrawCircleV(p, 2.0f + 2.5f*tt, Fade(PALETTE[PC_WHITE], 0.5f + 0.5f*tt));
    }
    // spawn pop-in: scale up from nothing over the first fraction of a second
    float scl = e->spawnScale < 1 ? (0.3f + 0.7f*e->spawnScale) : 1.0f;
    const Texture2D *etex = T->sprite ? sprite_get(T->sprite) : NULL;
    if (etex) {
        sprite_draw(T->sprite, p, etex->width * scl, 0, spriteTint);
        // damage blink: re-draw the sprite additively so it flashes white-hot
        if (e->hitFlash > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            sprite_draw(T->sprite, p, etex->width * scl, 0, Fade(WHITE, e->hitFlash/0.07f));
            EndBlendMode();
        }
    } else {
        geometry_draw(&T->geo, p, r*scl, c, rt);   // fallback: data-defined silhouette
        if (e->hitFlash > 0) DrawCircleV(p, r*scl, Fade(WHITE, 0.7f*e->hitFlash/0.07f));
    }
    // freeze/slow ring + mark reticle
    if (e->effTimer[EFFECT_FREEZE] > 0)
        DrawCircleLines((int)p.x,(int)p.y, r*1.3f, Fade(EFFECTS[EFFECT_FREEZE].tint, 0.7f));
    else if (e->effTimer[EFFECT_SLOW] > 0)
        DrawCircleLines((int)p.x,(int)p.y, r*1.25f, Fade(EFFECTS[EFFECT_SLOW].tint, 0.5f));
    if (e->effTimer[EFFECT_MARK] > 0)
        DrawPoly(p, 4, r*1.5f, rt*90, Fade(WHITE, 0.7f));
}

void render_arena(GameWorld *w) {
    Biome *bm = &BIOMES[w->biome];
    ClearBackground(bm->background);
    float rt = (float)w->runTime;
    // generated scrolling terrain loop (240x960 strip drawn at 3x -> 720x2880),
    // wraps seamlessly; solid color + stars remain the fallback and the overlay
    const Texture2D *bg = bm->slug ? sprite_get(TextFormat("backgrounds/%s/scroll.png", bm->slug)) : NULL;
    if (bg) {
        float scale = (float)SCREEN / bg->width;
        float h = bg->height * scale;
        float off = fmodf(rt * 30.0f, h);            // slow downward drift
        DrawTextureEx(*bg, (Vector2){0, off - h}, 0, scale, WHITE);
        DrawTextureEx(*bg, (Vector2){0, off}, 0, scale, WHITE);
    }
    // parallax stars: hashed scatter — the old i*constant positions formed a
    // lattice that read as diagonal stripes. Each star gets a stable x/phase and
    // a biome star_color from its hash (mostly the layer's shade, ~1 in 8 swaps
    // to another of the biome's three for sparkle variety).
    for (int layer=0; layer<3; layer++){ float speed=20+layer*35; int sz=1+layer, count=40;
        for (int i=0;i<count;i++){
            unsigned h = (unsigned)(i + layer*97) * 2654435761u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            int sx = (int)(h % SCREEN);
            float sy = fmodf((float)((h >> 10) % SCREEN) + rt*speed, (float)SCREEN);
            Color c = bm->stars[((h >> 20) & 7u) ? layer : (int)((h >> 23) % 3u)];
            DrawRectangle(sx,(int)sy,sz,sz,c); } }
}

void render_world(GameWorld *w) {
    Player *pl = &w->player;
    float rt = (float)w->runTime;
    // burning goo puddles (ground layer)
    for (int i=0;i<MAX_PUDDLES;i++){ Puddle *q=&w->puddles.items[i]; if(!q->active)continue;
        float a = fminf(1.0f, q->life/0.6f);
        DrawCircleV(q->pos, q->radius, Fade(PALETTE[PC_LIME], 0.25f*a));
        DrawCircleV(q->pos, q->radius*0.6f, Fade(PALETTE[PC_PEACH], 0.30f*a)); }
    for (int i=0;i<MAX_GEMS;i++){ Gem *g=&w->gems.items[i]; if(!g->active)continue;
        Color gc = BIOMES[w->biome].gemColor;   // one gem color per biome, anti-bullet by design
        if (!sprite_draw("sprites/common/gem.png", g->pos, 0, 0, gc))
            draw_gem(g->pos, 6.5f, gc, rt, i); }
    // power-up pickups: generated pixel-art badges (bobbing); glyph badge is the fallback
    for (int i=0;i<MAX_PICKUPS;i++){ Pickup *pk=&w->pickups.items[i]; if(!pk->active)continue;
        Color c=pickup_color(pk->kind);
        float bob = sinf(pk->bob)*3.0f; Vector2 pp = { pk->pos.x, pk->pos.y + bob };
        const char *spr = pickup_sprite(pk->kind);
        DrawCircleV(pp, 13, Fade(c,0.30f));
        if (!sprite_draw(spr, pp, 0, 0, WHITE)) {
            DrawPoly(pp, 6, 10, rt*60, c);
            DrawPolyLines(pp, 6, 10, rt*60, WHITE);
            const char *g = (pk->kind==PICK_MAGNET)?"MG":(pk->kind==PICK_WIPE)?"WP":(pk->kind==PICK_LEVELUP)?"LV":(pk->kind==PICK_HEAL)?"+1":"FZ";
            DrawText(g, (int)pp.x-MeasureText(g,10)/2, (int)pp.y-5, 10, WHITE); } }
    for (int i=0;i<MAX_ENEMIES;i++){ if(!w->enemies.items[i].active)continue; draw_enemy_shape(w,&w->enemies.items[i]); }
    if (w->boss.active){
        Boss *b=&w->boss;
        if (b->partCount > 0) {
            // multi-part: draw each alive part; the head pulses a shield ring while hands live
            bool handsLive=false;
            for (int i=0;i<b->partCount;i++) if (b->parts[i].alive && !b->parts[i].isHead) handsLive=true;
            for (int i=0;i<b->partCount;i++){ BossPartState *ps=&b->parts[i]; if(!ps->alive)continue;
                const BossPartDef *pd = &BIOMES[w->biome].boss.parts[i];
                Color body = pd->color.a ? pd->color : (ps->isHead ? PAL_BOSS : PALETTE[PC_CRIMSON]);
                if (pd->sprite && sprite_draw(pd->sprite, ps->pos, 0, ps->animRot, WHITE)) {
                    // generated pixel-art part (rotation follows the keyframe anim)
                } else if (pd->geo.count > 0) {
                    // authored silhouette, rotated by the keyframe anim around the part centre
                    rlPushMatrix();
                    rlTranslatef(ps->pos.x, ps->pos.y, 0);
                    rlRotatef(ps->animRot, 0, 0, 1);
                    geometry_draw(&pd->geo, (Vector2){0,0}, ps->radius, body, rt);
                    rlPopMatrix();
                } else {
                    DrawCircleV(ps->pos, ps->radius, body);
                    DrawCircleV(ps->pos, ps->radius*0.6f, PAL_BOSS_CORE);
                }
                if (ps->isHead && handsLive)                       // shielded look
                    DrawCircleLines((int)ps->pos.x,(int)ps->pos.y, ps->radius+6, Fade(PALETTE[PC_ICE], 0.5f+0.3f*sinf(rt*6)));
                // per-part hp pip above the part
                float pw=ps->radius*2; DrawRectangle((int)(ps->pos.x-ps->radius),(int)(ps->pos.y-ps->radius-10),(int)pw,4,Fade(PALETTE[PC_WINE],0.8f));
                DrawRectangle((int)(ps->pos.x-ps->radius),(int)(ps->pos.y-ps->radius-10),(int)(pw*(ps->hp/ps->hpMax)),4,PAL_BOSSBAR);
                // tether the hands to the head
                if (!ps->isHead) for (int h=0;h<b->partCount;h++) if (b->parts[h].isHead && b->parts[h].alive)
                    DrawLineEx(ps->pos, b->parts[h].pos, 3, Fade(PALETTE[PC_MAROON],0.5f));
            }
        } else {
            const BossDef *bd = &BIOMES[w->biome].boss;
            if (!(bd->sprite && sprite_draw(bd->sprite, b->pos, 0, 0, WHITE))) {
                DrawCircleV(b->pos,b->radius,PAL_BOSS);
                DrawCircleV(b->pos,b->radius*0.6f,PAL_BOSS_CORE);
            }
            // below the player's shield bar (y14-26) + hearts (y~32-48): no overlap
            float bw=SCREEN-80;
            DrawRectangle(40,56,(int)bw,14,Fade(PALETTE[PC_WINE],0.8f));
            DrawRectangle(40,56,(int)(bw*(b->hp/b->hpMax)),14,PAL_BOSSBAR);
        }
    }
    for (int i=0;i<MAX_BULLETS;i++){ Bullet *b=&w->bullets.items[i]; if(!b->active)continue;
        float ang = atan2f(b->vel.y, b->vel.x);
        switch (b->style) {
            case BSTYLE_BEAM: {   // elongated streak along travel
                Vector2 tail = { b->pos.x - cosf(ang)*b->radius*4, b->pos.y - sinf(ang)*b->radius*4 };
                DrawLineEx(tail, b->pos, b->radius*1.6f, b->color);
                DrawCircleV(b->pos, b->radius*0.8f, WHITE); break; }
            case BSTYLE_SLUG:     // fat glowing slug
                DrawCircleV(b->pos, b->radius, b->color);
                DrawCircleV(b->pos, b->radius*0.6f, WHITE); break;
            case BSTYLE_STAR:     // spinning star (chaos)
                DrawPoly(b->pos, 5, b->radius*1.4f, (float)w->runTime*360, b->color); break;
            case BSTYLE_DIAMOND:  // void diamond
                DrawPoly(b->pos, 4, b->radius*1.3f, 45+ang*RAD2DEG, b->color);
                DrawCircleV(b->pos, b->radius*0.4f, WHITE); break;
            case BSTYLE_RING:     // explosive ring shell
                DrawCircleV(b->pos, b->radius, Fade(b->color,0.5f));
                DrawCircleLines((int)b->pos.x,(int)b->pos.y, b->radius, b->color); break;
            case BSTYLE_BLOB: {   // wobbling goo wad with a drip
                float wob = 1.0f + sinf((float)w->runTime*14 + b->pos.x)*0.18f;
                DrawCircleV(b->pos, b->radius*wob, b->color);
                DrawCircleV((Vector2){b->pos.x, b->pos.y+b->radius*0.5f}, b->radius*0.55f, b->color);
                DrawCircleV(b->pos, b->radius*0.4f, Fade(WHITE,0.4f)); break; }
            default:              // BSTYLE_DOT
                if (!b->friendly) {
                    // enemy shots must read on EVERY background: black diamond backdrop
                    // (4 tris), ONE round body layer, square white core — cheap and spiky.
                    // Body color is the BIOME's bullet color (hand-picked vs terrain + gems),
                    // never the enemy's own hue: a bullet always looks like a bullet.
                    DrawPoly(b->pos, 4, b->radius*1.6f + 1.5f, 0, Fade(BLACK, 0.85f));
                    DrawCircleV(b->pos, b->radius*1.2f, BIOMES[w->biome].bulletColor);
                    float cs = b->radius*0.45f;
                    DrawRectangle((int)(b->pos.x-cs), (int)(b->pos.y-cs), (int)(cs*2), (int)(cs*2), WHITE);
                } else {
                    // player shots are STREAKS along their heading — instantly distinct
                    // from the round rimmed enemy rounds
                    Vector2 tail = { b->pos.x - cosf(ang)*b->radius*3.2f,
                                     b->pos.y - sinf(ang)*b->radius*3.2f };
                    DrawLineEx(tail, b->pos, b->radius*1.3f, Fade(b->color, 0.55f));
                    DrawLineEx(Vector2Lerp(tail, b->pos, 0.5f), b->pos, b->radius*1.1f, b->color);
                }
                break;
        } }
    // expanding shockwave rings (kill pops, explosions, stage clears)
    for (int i=0;i<MAX_RINGS;i++){ Ring *rg=&w->fx.rings[i]; if(rg->life<=0)continue;
        float t = 1.0f - rg->life/rg->maxLife;
        float rr = rg->r0 + (rg->r1 - rg->r0)*t;
        DrawRing(rg->pos, rr - rg->thick*0.5f, rr + rg->thick*0.5f, 0, 360, 20,
                 Fade(rg->color, rg->life/rg->maxLife)); }
    // orbital / aura visuals are driven by owned weapons in the loadout
    for (int i=0;i<w->loadout.count;i++){ Weapon *wp=&w->loadout.items[i];
        WeaponStats st=weapon_stats(wp);
        if (wp->type==WP_ORBITAL){ int balls=2+st.multi; if(balls>MAX_ORBITALS)balls=MAX_ORBITALS;
            float orbitR=46+wp->level*6+(st.evolved?24:0); float ang=(float)w->runTime*3.0f;
            for (int b=0;b<balls;b++){ float a=ang+(float)b/balls*2*PI;
                DrawCircleV((Vector2){pl->pos.x+cosf(a)*orbitR,pl->pos.y+sinf(a)*orbitR},7,PALETTE[PC_ICE]); } }
        if (wp->type==WP_AURA)
            DrawCircleLines((int)pl->pos.x,(int)pl->pos.y,60+wp->level*14+st.multi*16+(st.evolved?40:0),
                            Fade(PALETTE[PC_CORAL],0.4f)); }
    // companion drones: small spinning diamonds in their weapon's colour, with a link line
    for (int i=0;i<w->drones.count;i++){ Drone *dr=&w->drones.items[i]; if(!dr->active)continue;
        Color dc = WEAPONS_DEF[dr->weapon.type].color;
        DrawLineEx(pl->pos, dr->pos, 1.0f, Fade(dc,0.18f));        // tether to the ship
        DrawCircleV(dr->pos, 9, Fade(dc,0.35f));
        if (!sprite_draw("sprites/common/drone.png", dr->pos, 0, 0, dc)) {
            DrawPoly(dr->pos, 4, 7, rt*120 + i*40, dc);
            DrawCircleV(dr->pos, 2.5f, WHITE); }
        if (dr->weapon.evolution>=0) DrawPolyLines(dr->pos, 4, 10, rt*120+i*40, PAL_GOLD); }
    bool rolling = pl->rollTimer > 0;
    // during a barrel roll the ship spins fast + leaves a bright afterimage; always visible
    bool show = rolling || !(pl->iframe>0 && ((int)(pl->iframe*18)%2));
    if (show){
        float spin = rolling ? (1.0f - pl->rollTimer/0.28f) * 720.0f : 0.0f;  // one fast revolution
        if (rolling) {
            // motion-blur afterimage behind the dash + a bright dash ring
            Vector2 back = Vector2Subtract(pl->pos, Vector2Scale(pl->rollDir, 16));
            DrawCircleV(back, pl->radius+2, Fade(PAL_PLAYER_GLOW, 0.25f));
            DrawCircleLines((int)pl->pos.x,(int)pl->pos.y, pl->radius+7, Fade(PALETTE[PC_PALECYAN], 0.6f));
        }
        // the cannon lives UNDER the ship's nose and does the recoiling; the hull stays planted
        if (!rolling) {
            float mf = w->muzzleFlash > 0 ? w->muzzleFlash/0.06f : 0;
            float kick = 5.0f * mf * mf;   // sharp kick-back, fast settle
            Vector2 cann = { pl->pos.x, pl->pos.y - pl->radius - 7 + kick };
            if (!sprite_draw("sprites/common/cannon.png", cann, 0, 0, WHITE))
                DrawRectangle((int)cann.x-3, (int)cann.y-8, 6, 12, PALETTE[PC_STEEL]);
            if (mf > 0) {
                Vector2 nose = { pl->pos.x, pl->pos.y - pl->radius - 17 + kick };
                DrawPoly(nose, 4, 3.0f + 5.0f*mf, rt*400, Fade(PALETTE[PC_GOLD], 0.8f*mf));
                DrawCircleV(nose, 2.0f + 2.5f*mf, Fade(WHITE, 0.9f*mf));
            }
        }
        // charge-shot feedback: growing orb at the nose + a stepped bar under the ship
        if (pl->charge > 0.04f && !rolling) {
            float u = pl->charge / CHARGE_FULL_TIME;
            int lvl = pl->charge >= CHARGE_FULL_TIME ? 2 : (pl->charge >= CHARGE_LV1_TIME ? 1 : 0);
            bool full = (lvl == 2);
            Color oc = full ? PAL_GOLD : (lvl == 1 ? PALETTE[PC_AQUABRT] : PALETTE[PC_SKY]);
            Vector2 nose = { pl->pos.x, pl->pos.y - pl->radius - 17 };
            float orbR = 2.0f + 6.0f*u + (full ? 1.5f*sinf(rt*22) : 0);
            DrawPoly(nose, 4, orbR, rt*260, Fade(oc, 0.85f));
            DrawPoly(nose, 4, orbR*0.55f, -rt*180, WHITE);
            int cbw = 24, cbx = (int)roundf(pl->pos.x) - cbw/2, cby = (int)roundf(pl->pos.y) + 24;
            DrawRectangle(cbx, cby, cbw, 3, Fade(PAL_PANEL, 0.85f));
            DrawRectangle(cbx, cby, (int)(cbw*u), 3, (full && ((int)(rt*8))%2) ? RAYWHITE : oc);
            int notch = (int)(cbw * (CHARGE_LV1_TIME / CHARGE_FULL_TIME));   // the LV1 step
            DrawRectangle(cbx + notch, cby - 1, 1, 5, PAL_TEXT_DIM);
        }
        // animated engine exhaust: whole-pixel flicker jets under the ship's 4 nozzles
        // (center flames end at the sprite's bottom row 36; pod flames end at row 32),
        // quantized to 12fps so the flicker reads as chunky pixel animation
        const Texture2D *ptex = sprite_get("sprites/common/player.png");
        if (!rolling && ptex) {
            int bleft = (int)roundf(pl->pos.x - ptex->width*0.5f);
            int btop  = (int)roundf(pl->pos.y - ptex->height*0.5f);
            int shear = (int)roundf(pl->bank * -2.0f);   // tail swings away from the turn
            int fr = (int)(rt*12.0f);
            static const int JCOL[4]={12,21,6,27}, JROW[4]={36,36,32,32};
            static const int JPAL[3]={PC_GOLD,PC_PEACH,PC_CORAL};
            for (int e=0;e<4;e++){
                unsigned hsh = (unsigned)fr*2654435761u ^ (unsigned)e*40503u;
                DrawRectangle(bleft+JCOL[e]+shear, btop+JROW[e], 3, 1+(int)(hsh%3u),
                              PALETTE[JPAL[(hsh>>4)%3]]);
            }
        }
        bool drawn = rolling ? sprite_draw("sprites/common/player.png", pl->pos, 0, spin, WHITE)
                             : sprite_draw_banked("sprites/common/player.png", pl->pos, pl->bank, WHITE);
        if (!drawn) {
            DrawPoly(pl->pos,3,pl->radius+2,-90+spin,PAL_PLAYER); // ship points up (spins while rolling)
            DrawCircleV(pl->pos,pl->radius*0.4f,WHITE); }
    }
    // charge lock-on reticle: rotating diamond + orbiting ticks on the target
    if (pl->lockOn && pl->charge >= CHARGE_LV1_TIME) {
        Vector2 tp = {0,0}; float trr = 14; bool okL = false;
        if (pl->lockIsBoss) {
            if (w->boss.active) {
                if (pl->lockIdx >= 0 && pl->lockIdx < w->boss.partCount && w->boss.parts[pl->lockIdx].alive) {
                    tp = w->boss.parts[pl->lockIdx].pos; trr = w->boss.parts[pl->lockIdx].radius; okL = true;
                } else if (pl->lockIdx < 0) { tp = w->boss.pos; trr = w->boss.radius; okL = true; }
            }
        } else if (pl->lockIdx >= 0 && w->enemies.items[pl->lockIdx].active) {
            tp = w->enemies.items[pl->lockIdx].pos; trr = w->enemies.items[pl->lockIdx].radius; okL = true;
        }
        if (okL) {
            bool full = pl->charge >= CHARGE_FULL_TIME;
            Color lc = full ? (((int)(rt*10))%2 ? RAYWHITE : PAL_GOLD) : PALETTE[PC_AQUABRT];
            float rr = trr + 7 + 2.0f*sinf(rt*10);
            DrawPolyLinesEx(tp, 4, rr, rt*120, 2, lc);
            for (int c2 = 0; c2 < 4; c2++) {
                float a = rt*2.0f + c2*(PI/2);
                DrawRectangle((int)(tp.x + cosf(a)*rr)-1, (int)(tp.y + sinf(a)*rr)-1, 3, 3, lc);
            }
        }
    }
    if (!pl->autofire)
        hint_walk(SETTINGS.autoCharge ? "AUTO-FIRE OFF (charging idle)  {auto}" : "AUTO-FIRE OFF  {auto}",
                  10, SCREEN-24, 14, Fade(PAL_TEXT_DIM, 0.8f), true);
    for (int i=0;i<MAX_DMGNUM;i++){ DamageNum *d=&w->fx.dmg[i]; if(d->life<=0)continue;
        Color c=d->color; c.a=(unsigned char)(255*fminf(1.0f,d->life/0.4f));
        DrawText(TextFormat("%d",d->amount),(int)d->pos.x,(int)d->pos.y,d->crit?18:14,c); }
    for (int i=0;i<MAX_PARTICLES;i++){ Particle *pt=&w->fx.parts[i]; if(pt->life<=0)continue;
        float a=pt->life/pt->maxLife; Color c=pt->color; c.a=(unsigned char)(a*255);
        // square pixel particles: 2 triangles instead of a ~36-triangle circle fan —
        // thousands can be alive during a board wipe, and squares read MORE pixel-art
        float s = pt->size*a; if (s < 1) s = 1;
        DrawRectangle((int)(pt->pos.x - s), (int)(pt->pos.y - s), (int)(s*2), (int)(s*2), c); }
    // lightning bolts (bright, fading; a thick glow + a white-hot core)
    for (int i=0;i<MAX_BOLTS;i++){ Bolt *bo=&w->fx.bolts[i]; if(bo->life<=0)continue;
        float a=bo->life/bo->maxLife;
        DrawLineEx(bo->a, bo->b, 4.0f, Fade(bo->color, 0.5f*a));
        DrawLineEx(bo->a, bo->b, 1.6f, Fade(WHITE, a)); }
    if (w->hitFlash>0.02f) DrawRectangle(0,0,SCREEN,SCREEN,Fade(PAL_HITFLASH,w->hitFlash*0.3f));
}

void render_hud(GameWorld *w) {
    Player *pl = &w->player;
    float rt = (float)w->runTime;
    float sFrac = pl->shield / pl->shieldMax;
    int sx=14, sy=14, sw=200, sh=12;
    DrawRectangle(sx,sy,sw,sh,Fade(PALETTE[PC_NAVY],0.85f));
    DrawRectangle(sx,sy,(int)(sw*sFrac),sh,PAL_SHIELD);
    // pip dividers: one segment per hit the shield can eat (shield_hit = 5 dmg per pip)
    for (float tcap=5.0f; tcap < pl->shieldMax; tcap+=5.0f) {
        int tx3 = sx + (int)(sw * tcap / pl->shieldMax);
        DrawRectangle(tx3-2, sy, 3, sh, Fade(BLACK, 0.7f));
    }
    // shield fully down: the bar frame pulses red — "you are one hit from a heart"
    if (pl->shield <= 0)
        DrawRectangleLinesEx((Rectangle){sx-2,sy-2,sw+4,sh+4}, 2,
                             Fade(PALETTE[PC_CORAL], 0.5f + 0.4f*sinf(rt*10)));
    // Shield-recharge indicator so you can SEE it coming back. The label sits to the RIGHT of the
    // bar (x past sw); the bar is y14..26 and the HP hearts are at y40, so nothing overlaps.
    if (pl->shield < pl->shieldMax) {
        int tx = sx + sw + 8;
        if (pl->shieldRegenTimer > 0) {
            // DELAY phase: regen hasn't started. An amber sliver shrinks across the empty part
            // toward where the bar will resume — a visible "charge pending" countdown.
            float lockFrac = pl->shieldRegenTimer / pl->shieldRegenDelay;   // 1 -> 0
            int lx = sx + (int)(sw*sFrac);
            DrawRectangle(lx, sy, (int)((sw-(sw*sFrac))*lockFrac), sh, Fade(PALETTE[PC_CLAY], 0.7f));
            DrawText("charging", tx, sy, 13, PALETTE[PC_CLAY]);
        } else {
            // ACTIVE regen: a pulsing bright cap at the growing edge of the shield.
            int ex = sx + (int)(sw*sFrac);
            float pulse = 0.5f + 0.5f*sinf(rt*12);
            DrawRectangle(ex-2, sy, 4, sh, Fade(WHITE, pulse));
            DrawText("+++", tx, sy+1, 11, Fade(PAL_SHIELD, 0.6f+0.4f*pulse));
        }
    }
    // HP pips. A FILLED bright pip = have it; a missing pip = a hollow dim ring (not a near-same
    // red) so losses read instantly. Hearts row sits well below the shield bar.
    for (int i=0;i<pl->hpMax;i++){
        int cx=22+i*22, cy=40;
        if (i < pl->hp) DrawCircle(cx,cy,8,PAL_HP);
        else { DrawCircleLines(cx,cy,8,Fade(PAL_TEXT_DIM,0.6f));   // empty socket: hollow + dim
               DrawCircle(cx,cy,6,Fade(PALETTE[PC_INK],0.7f)); }
    }
    float xpFrac=(float)(pl->xp/pl->xpToNext);
    DrawRectangle(0,SCREEN-10,SCREEN,10,Fade(PAL_PANEL,0.85f));
    DrawRectangle(0,SCREEN-10,(int)(SCREEN*xpFrac),10,PAL_XP);
    DrawText(TextFormat("LV %d",pl->level),8,SCREEN-32,22,PAL_TEXT);
    DrawText(TextFormat("%05.0f",w->runTime),SCREEN-90,14,22,PAL_TEXT);
    DrawText(TextFormat("KILLS %d",w->kills),SCREEN-150,40,18,PAL_GOLD);
    DrawText(TextFormat("STAGE %d",w->stage),SCREEN-150,60,18,PALETTE[PC_LAVENDER]);
    if (w->loopCount > 0) DrawText(TextFormat("LOOP %d",w->loopCount+1),SCREEN-150,78,16,PALETTE[PC_CORAL]);
    // Frenzy overdrive banner while the pickup buff is active
    if (pl->frenzyTimer > 0) {
        float pulse = 0.6f+0.4f*sinf((float)w->runTime*10);
        const char *f="FRENZY"; int fw=MeasureText(f,22);
        DrawText(f, SCREEN/2-fw/2, 40, 22, Fade(PALETTE[PC_CORAL], pulse));
        const char *fsub="+50% dmg  x1.8 rate";
        DrawText(fsub, SCREEN/2-MeasureText(fsub,12)/2, 64, 12, Fade(PALETTE[PC_CORAL], pulse*0.7f));
        DrawRectangle(SCREEN/2-50, 78, (int)(100*fminf(1.0f,pl->frenzyTimer/8.0f)), 4, PALETTE[PC_CORAL]);
    }
    if (SETTINGS.showFps) DrawFPS(SCREEN/2-20,14);
}

// describe a card's title + subtitle + accent color + icon sprite (NULL = no icon)
static void card_text(const Card *c, const char **title, const char **sub, Color *accent, const char **sprite) {
    *sprite = NULL;
    switch (c->kind) {
        case CARD_RUNE: { const RuneDef *r=&RUNES[c->value];
            *title=r->name; *sub=r->stat; *accent=r->color; *sprite=r->sprite; break; }
        case CARD_NEW_WEAPON: { const WeaponDef *d=&WEAPONS_DEF[c->value];
            static char wpbuf[64];
            snprintf(wpbuf, sizeof(wpbuf), "dmg %.0f  rate %.2fs", d->baseDamage, d->baseInterval);
            *title=d->name; *sub=wpbuf; *accent=d->color; *sprite=d->sprite; break; }
        case CARD_LEVEL_WEAPON: { const WeaponDef *d=&WEAPONS_DEF[c->value];
            *title=d->name; *sub="LEVEL UP  +dmg & rate"; *accent=d->color; *sprite=d->sprite; break; }
        case CARD_PASSIVE: { const Passive *p=&PASSIVES[c->value];
            *title=p->name; *sub=p->desc; *accent=PAL_TEXT_DIM; *sprite=p->sprite; break; }
        case CARD_DRONE: { *title=drone_ai_name(c->value); *sub="NEW DRONE";
            *accent=PALETTE[PC_AQUABRT]; *sprite=drone_ai_sprite(c->value); break; }
        default: *title="?"; *sub=""; *accent=WHITE; break;
    }
}

// draw one weapon's 3 sockets. `selected` rings it; `glow` outlines it gold if a trifecta
// would complete; `nextSocket` (or -1) marks the socket a rune would drop into.
static void draw_weapon_sockets(Weapon *wp, int x, int y, int w_box, int h_box,
                                bool selected, bool glow, int nextSocket, bool dimmed) {
    Color border = glow ? PAL_GOLD : (selected?PAL_TEXT:PALETTE[PC_GRAPE]);
    DrawRectangle(x,y,w_box,h_box, dimmed?PALETTE[PC_BLACK]:PAL_PANEL);
    DrawRectangleLinesEx((Rectangle){x,y,w_box,h_box}, glow?4:(selected?3:1), border);
    const WeaponDef *d=&WEAPONS_DEF[wp->type];
    DrawText(d->name, x+8, y+6, 14, dimmed?PALETTE[PC_PLUM]:RAYWHITE);
    DrawText(TextFormat("LV%d", wp->level), x+w_box-MeasureText(TextFormat("LV%d",wp->level),12)-8, y+6, 12, Fade(PAL_GOLD,0.85f));
    if (wp->evolution>=0) DrawText(TRIFECTAS[wp->evolution].name, x+8, y+22, 12, PAL_GOLD);
    int sr=11, sy=y+h_box-22;
    for (int s=0;s<SOCKETS;s++){ int sx=x+16+s*30;
        if (wp->runes[s]>=0) { const RuneDef *rd=&RUNES[wp->runes[s]];
            if (!(rd->sprite && sprite_draw(rd->sprite, (Vector2){(float)sx,(float)sy}, sr*2, 0, WHITE)))
                DrawCircle(sx,sy,sr,rd->color); }
        else { DrawCircleLines(sx,sy,sr,PALETTE[PC_PLUM]);
            // neutral marker for where the rune lands; gold is reserved for the trifecta glow
            if (s==nextSocket) DrawCircleLines(sx,sy,sr+3,PAL_TEXT_DIM); }
    }
}

void render_card_screen(GameWorld *w) {
    DrawRectangle(0,0,SCREEN,SCREEN,Fade(BLACK,0.65f));
    Cards *cs=&w->cardsel;

    if (cs->phase==PICK_CARD) {
        const char *t="LEVEL UP"; DrawText(t,SCREEN/2-MeasureText(t,44)/2,80,44,PAL_GOOD);
        DrawText(TextFormat("LV %d",w->player.level),SCREEN/2-24,130,22,RAYWHITE);
        int cw=200,ch=240,gap=16,totalW=cw*3+gap*2,x0=SCREEN/2-totalW/2,y=210;
        for (int i=0;i<3;i++){ int x=x0+i*(cw+gap); bool hov=(i==cs->hover);
            const char *title,*sub,*sprite; Color accent;
            card_text(&g_cards[i],&title,&sub,&accent,&sprite);
            DrawRectangle(x,y,cw,ch,PAL_PANEL);
            DrawRectangleLinesEx((Rectangle){x,y,cw,ch},hov?4:2, hov?PAL_GOLD:accent);
            if (!(sprite && sprite_draw(sprite, (Vector2){x+cw/2.0f,y+60}, 44, 0, WHITE)))
                if (g_cards[i].kind==CARD_RUNE) DrawCircle(x+cw/2,y+60,22,accent);
            DrawText(title,x+cw/2-MeasureText(title,20)/2,y+96,20,RAYWHITE);
            draw_text_wrapped(sub, x+12, y+124, 15, cw-24, accent);
            DrawText(TextFormat("[%d]",i+1),x+cw/2-10,y+ch-32,20,PAL_TEXT_DIM); }
        draw_hint("{menu} choose   -   {confirm} accept", SCREEN/2, y+ch+22, 18, PAL_TEXT_DIM);
        // show the loadout small at the bottom so players see their weapons/sockets
        int n=w->loadout.count, bw=114, bh=70, g2=8, tw=n*bw+(n-1)*g2, bx=SCREEN/2-tw/2, by=SCREEN-96;
        for (int i=0;i<n;i++) draw_weapon_sockets(&w->loadout.items[i], bx+i*(bw+g2), by, bw, bh, false, false, -1, false);
        return;
    }

    // PICK_SOCKET: place the chosen rune
    const RuneDef *r=&RUNES[cs->placingRune];
    const char *t="PLACE RUNE"; DrawText(t,SCREEN/2-MeasureText(t,40)/2,70,40,PAL_GOOD);
    if (!(r->sprite && sprite_draw(r->sprite, (Vector2){SCREEN/2.0f,140}, 40, 0, WHITE)))
        DrawCircle(SCREEN/2,140,20,r->color);
    DrawText(TextFormat("%s  %s",r->name,r->stat),SCREEN/2-MeasureText(TextFormat("%s  %s",r->name,r->stat),18)/2,168,18,r->color);

    // placement targets = loadout weapons + drone weapons (combined index space)
    int n=placement_count(w), bw=210, bh=96, gap=14, cols=2;
    int rows=(n+cols-1)/cols, totalW=cols*bw+(cols-1)*gap;
    int x0=SCREEN/2-totalW/2, y0=210;
    for (int i=0;i<n;i++){ int cx=i%cols, cy=i/cols;
        int x=x0+cx*(bw+gap), y=y0+cy*(bh+gap);
        Weapon *wp=placement_weapon(w, i);
        bool isDrone = (i >= w->loadout.count);
        bool selected=(i==cs->hover);
        int slot = rune_first_open_socket(w, i);             // where the rune would land
        bool full = (slot < 0);
        int evo = full ? -1 : rune_would_evolve(w, i, slot, cs->placingRune);
        draw_weapon_sockets(wp, x, y, bw, bh, selected, evo>=0, slot, full);
        if (isDrone) DrawText("DRONE", x+8, y+bh-18, 11, PALETTE[PC_AQUABRT]);
        if (evo>=0) { const char *en=TRIFECTAS[evo].name;
            DrawText(TextFormat("-> %s!",en), x+bw-MeasureText(TextFormat("-> %s!",en),12)-8, y+6, 12, PAL_GOLD); }
        if (full) DrawText("FULL", x+bw-MeasureText("FULL",12)-8, y+6, 12, PALETTE[PC_MAUVEDK]);
        (void)rows;
    }
    const char *hint=pad_on()?"D-pad: pick weapon - Cross to socket":"arrows: pick weapon  /  Enter to socket";
    DrawText(hint,SCREEN/2-MeasureText(hint,18)/2,SCREEN-50,18,PAL_TEXT_DIM);
}

void render_title(GameWorld *w) {
    if (!sprite_draw("sprites/common/player.png", (Vector2){SCREEN/2.0f, 90}, 64, 0, WHITE))
        DrawPoly((Vector2){SCREEN/2.0f, 90}, 3, 26, -90, PALETTE[PC_CORAL]);
    const char *title="ANGERBLAST"; int fs=64;
    DrawText(title,SCREEN/2-MeasureText(title,fs)/2+3,160+3,fs,PALETTE[PC_WINE]);
    DrawText(title,SCREEN/2-MeasureText(title,fs)/2,160,fs,PALETTE[PC_CORAL]);
    const char *sub="vertical schmup roguelite";
    DrawText(sub,SCREEN/2-MeasureText(sub,24)/2,236,24,PALETTE[PC_PEACH]);
    draw_hint("move {move}   -   barrel-roll {dodge}", SCREEN/2, 346, 20, RAYWHITE);
    draw_hint("hold {fire} CHARGE SHOT   -   {auto} auto-fire", SCREEN/2, 376, 20, RAYWHITE);
    draw_hint("grab XP, snag power-ups, socket runes, build drones", SCREEN/2, 406, 18, PAL_TEXT_DIM);
    draw_hint("pause {pause}   -   codex {codex}   -   kill the boss to advance", SCREEN/2, 432, 18, PAL_TEXT_DIM);
    if (((int)(GetTime()*2))%2)
        draw_hint("press {confirm} to begin", SCREEN/2, 520, 28, PAL_GOLD);
    if (w->bestScore>0) DrawText(TextFormat("best score: %.0f",w->bestScore),
        SCREEN/2-MeasureText(TextFormat("best score: %.0f",w->bestScore),20)/2,580,20,PAL_TEXT_DIM);
}

void render_dead(GameWorld *w) {
    DrawRectangle(0,0,SCREEN,SCREEN,Fade(BLACK,0.6f));
    if (!sprite_draw("sprites/common/player.png", (Vector2){SCREEN/2.0f, 120}, 56, 0, Fade(PALETTE[PC_CORAL], 0.85f)))
        DrawPoly((Vector2){SCREEN/2.0f, 120}, 3, 24, -90, Fade(PALETTE[PC_CORAL], 0.85f));
    const char *d="YOU DIED"; DrawText(d,SCREEN/2-MeasureText(d,60)/2,200,60,PALETTE[PC_CORAL]);
    DrawText(TextFormat("survived %.0fs",w->runTime),SCREEN/2-MeasureText(TextFormat("survived %.0fs",w->runTime),26)/2,300,26,RAYWHITE);
    const char *st=TextFormat("level %d   kills %d   stage %d",w->player.level,w->kills,w->stage);
    DrawText(st,SCREEN/2-MeasureText(st,22)/2,340,22,PAL_GOLD);
    DrawText(TextFormat("score %.0f",w->score),SCREEN/2-MeasureText(TextFormat("score %.0f",w->score),24)/2,376,24,PALETTE[PC_LAVENDER]);
    if (((int)(GetTime()*2))%2)
        draw_hint("{confirm} to try again", SCREEN/2, 460, 26, PAL_GOLD);
}

// One "LABEL    value" stat row, left label + right-aligned value within [x, x+w].
static void stat_row(int x, int y, int w_box, const char *label, const char *value, Color vcol) {
    DrawText(label, x, y, 18, PAL_TEXT_DIM);
    DrawText(value, x + w_box - MeasureText(value, 18), y, 18, vcol);
}

void render_pause(GameWorld *w) {
    DrawRectangle(0,0,SCREEN,SCREEN,Fade(BLACK,0.72f));
    Player *pl = &w->player;

    const char *t="PAUSED";
    DrawText(t,SCREEN/2-MeasureText(t,52)/2,54,52,PAL_TEXT);

    // ---- current stats panel ----
    int px=120, pw=SCREEN-240, py=140, ph=258;
    DrawRectangle(px,py,pw,ph,Fade(PAL_PANEL,0.9f));
    DrawRectangleLinesEx((Rectangle){px,py,pw,ph},2,PALETTE[PC_GRAPE]);
    const char *sh="STATS";
    DrawText(sh,px+pw/2-MeasureText(sh,20)/2,py+12,20,PAL_GOLD);

    // two side-by-side columns, each owning half the panel width
    int pad=28, gutter=32, ry=py+46, dy=26;
    int colW = (pw - pad*2 - gutter) / 2;
    int lx = px + pad;            // left column origin
    int cx = lx + colW + gutter;  // right column origin
    stat_row(lx,ry+dy*0, colW, "Time survived", TextFormat("%.0f s", w->runTime), PAL_TEXT);
    stat_row(lx,ry+dy*1, colW, "Score",         TextFormat("%.0f", w->score), PALETTE[PC_LAVENDER]);
    stat_row(lx,ry+dy*2, colW, "Level",         TextFormat("%d", pl->level), PAL_GOOD);
    stat_row(lx,ry+dy*3, colW, "Kills",         TextFormat("%d", w->kills), PAL_GOLD);
    stat_row(lx,ry+dy*4, colW, "Stage",         TextFormat("%d", w->stage), PALETTE[PC_LAVENDER]);
    stat_row(lx,ry+dy*5, colW, "Biome",
             w->loopCount > 0 ? TextFormat("%s (loop %d)", BIOMES[w->biome].name, w->loopCount+1)
                               : BIOMES[w->biome].name,
             PALETTE[PC_LAVENDER]);
    stat_row(lx,ry+dy*6, colW, "HP",            TextFormat("%d / %d", pl->hp, pl->hpMax), PAL_HP);
    stat_row(lx,ry+dy*7, colW, "Shield",        TextFormat("%.0f / %.0f", pl->shield, pl->shieldMax), PAL_SHIELD);

    // right column: combat stats (passives + runes folded into the player globals)
    stat_row(cx,ry+dy*0, colW, "Damage mult",    TextFormat("x%.2f", pl->globalDamageMul), PALETTE[PC_PEACH]);
    stat_row(cx,ry+dy*1, colW, "Fire-rate mult", TextFormat("x%.2f", 1.0f/(pl->globalRateMul>0?pl->globalRateMul:1.0f)), PALETTE[PC_PEACH]);
    stat_row(cx,ry+dy*2, colW, "Move speed",     TextFormat("%.0f", pl->moveSpeed), PAL_TEXT);
    stat_row(cx,ry+dy*3, colW, "Pickup range",   TextFormat("%.0f", pl->pickupRadius), PAL_TEXT);
    {   // Armor as the DR% it actually grants (diminishing): armor/(armor+K)
        float dr = pl->armor / (pl->armor + PLAYER_ARMOR_K);
        stat_row(cx,ry+dy*4, colW, "Armor",      pl->armor>0?TextFormat("%.0f%% dmg cut", dr*100):"-", PALETTE[PC_ICE]);
    }
    stat_row(cx,ry+dy*5, colW, "Pierce",         pl->piercing?TextFormat("+%d", pl->pierceCount):"-", PAL_TEXT);
    stat_row(cx,ry+dy*6, colW, "XP mult",        TextFormat("x%.2f", pl->xpMul), PAL_TEXT);
    stat_row(cx,ry+dy*7, colW, "Weapons",        TextFormat("%d / %d", w->loadout.count, MAX_WEAPONS), PAL_TEXT);

    // ---- SETTINGS (navigate: up/down, change: left/right or confirm) ----
    {
        int sx2=120, sw2=SCREEN-240, sy2=410, rh=28;
        DrawRectangle(sx2, sy2, sw2, 12 + 5*rh + 30, Fade(PAL_PANEL,0.9f));
        DrawRectangleLinesEx((Rectangle){sx2,sy2,(float)sw2,(float)(12+5*rh+30)},2,PALETTE[PC_GRAPE]);
        const char *sh2="SETTINGS";
        DrawText(sh2, sx2+sw2/2-MeasureText(sh2,18)/2, sy2+8, 18, PAL_GOLD);
        static const char *HS_NAMES[3] = { "OFF", "SINGLE", "ALL" };
        static const char *LABELS[5] = { "FPS OVERLAY", "FREEZE-FRAMES", "IDLE AUTO-CHARGE",
                                         "SFX VOLUME", "MUSIC VOLUME" };
        for (int r = 0; r < 5; r++) {
            int ry2 = sy2 + 36 + r*rh;
            bool sel = (w->codex.scroll == r);   // reuse codex.scroll as the pause cursor
            if (sel) DrawRectangle(sx2+8, ry2-4, sw2-16, rh-4, Fade(PAL_PANEL_HI, 0.35f));
            DrawText(LABELS[r], sx2+28, ry2, 18, sel ? PAL_TEXT : PAL_TEXT_DIM);
            if (r >= 3) {                                    // volume sliders
                float v = r==3 ? SETTINGS.sfxVol : SETTINGS.musicVol;
                int bw2=110, bx2=sx2+sw2-28-bw2-44, by2=ry2+4;
                DrawRectangle(bx2, by2, bw2, 10, Fade(PALETTE[PC_BLACK],0.7f));
                DrawRectangle(bx2, by2, (int)(bw2*v), 10, sel ? PAL_GOLD : PALETTE[PC_PERIWINKLE]);
                DrawRectangleLinesEx((Rectangle){(float)bx2,(float)by2,(float)bw2,10},1,PALETTE[PC_GRAPE]);
                DrawText(TextFormat("%d%%",(int)(v*100+0.5f)), sx2+sw2-28-MeasureText(TextFormat("%d%%",(int)(v*100+0.5f)),18), ry2, 18, sel ? PAL_GOLD : PAL_TEXT_DIM);
            } else {
                const char *value = r==0 ? (SETTINGS.showFps ? "ON" : "OFF")
                                  : r==1 ? HS_NAMES[SETTINGS.hitstopMode]
                                  : (SETTINGS.autoCharge ? "ON" : "OFF");
                DrawText(value, sx2+sw2-28-MeasureText(value,18), ry2, 18, sel ? PAL_GOLD : PAL_TEXT_DIM);
            }
        }
    }

    // ---- the loadout (weapons + sockets) along the bottom ----
    int n=w->loadout.count;
    if (n>0){
        int bw=114, bh=70, g2=8, tw=n*bw+(n-1)*g2, bx=SCREEN/2-tw/2, by=SCREEN-102;
        const char *lh="LOADOUT";
        DrawText(lh,SCREEN/2-MeasureText(lh,16)/2,by-20,16,PAL_TEXT_DIM);
        for (int i=0;i<n;i++) draw_weapon_sockets(&w->loadout.items[i], bx+i*(bw+g2), by, bw, bh, false, false, -1, false);
    }

    draw_hint("{pause} resume    {codex} codex    {menu} settings    {confirm} change",
              SCREEN/2, SCREEN-24, 16, PAL_TEXT_DIM);

}

// ---- in-game encyclopedia (codex) ----
static const char *CODEX_TABS[CODEX_TAB_COUNT] = { "ENEMIES","WEAPONS","EFFECTS","PICKUPS","RUNES","DRONES","PASSIVES" };

// gather all unique enemies across biomes into `out` (by name), returns the count.
static int codex_gather_enemies(const EnemyType **out, int cap) {
    int n = 0;
    for (int b = 0; b < NUM_BIOMES && n < cap; b++)
        for (int i = 0; i < BIOMES[b].rosterCount && n < cap; i++) {
            const EnemyType *et = &BIOMES[b].roster[i];
            bool dup = false;
            for (int k = 0; k < n; k++) if (out[k]->name && et->name && strcmp(out[k]->name, et->name)==0) dup = true;
            if (!dup) out[n++] = et;
        }
    return n;
}

int codex_entry_count(GameWorld *w, int tab) {
    (void)w;
    switch (tab) {
        case CODEX_ENEMIES: { const EnemyType *tmp[MAX_BIOMES*MAX_BIOME_ROSTER]; return codex_gather_enemies(tmp, MAX_BIOMES*MAX_BIOME_ROSTER); }
        case CODEX_WEAPONS: return WP_COUNT;
        case CODEX_EFFECTS: return EFFECT_COUNT;
        case CODEX_PICKUPS: return PICKUP_KIND_COUNT;
        case CODEX_RUNES:   return RUNE_COUNT + TRI_COUNT;   // runes then trifectas
        case CODEX_DRONES:  return DRONE_AI_COUNT;
        case CODEX_PASSIVES: return PASV_COUNT;
        default: return 0;
    }
}

// draw one codex row: a sprite portrait (or colour swatch), a bold title, and a dim subtitle.
static void codex_row_ex(int x, int y, Color swatch, const char *sprite, const char *title, const char *sub) {
    if (!(sprite && sprite_draw(sprite, (Vector2){x+14, y+18}, 30, 0, WHITE)))
        DrawCircle(x+12, y+14, 9, swatch);
    DrawText(title, x+34, y+2, 20, PAL_TEXT);
    if (sub && sub[0]) DrawText(sub, x+34, y+24, 15, PAL_TEXT_DIM);
}
void render_codex(GameWorld *w) {
    DrawRectangle(0,0,SCREEN,SCREEN,Fade(BLACK,0.85f));
    Codex *cx = &w->codex;
    const char *t="CODEX"; DrawText(t,SCREEN/2-MeasureText(t,40)/2,20,40,PAL_GOLD);

    // tab bar
    int tabW = SCREEN/CODEX_TAB_COUNT;
    for (int i=0;i<CODEX_TAB_COUNT;i++){ bool sel=(i==cx->tab);
        int tx=i*tabW;
        DrawRectangle(tx,70,tabW-2,28, sel?PAL_PANEL_HI:PAL_PANEL);
        DrawText(CODEX_TABS[i], tx+tabW/2-MeasureText(CODEX_TABS[i],14)/2, 76, 14, sel?BLACK:PAL_TEXT_DIM); }

    int total = codex_entry_count(w, cx->tab);
    int rowH = 46, top = 112, bottom = SCREEN-46;
    int visible = (bottom-top)/rowH;
    if (cx->scroll > total-visible) cx->scroll = (total>visible)?total-visible:0;
    if (cx->scroll < 0) cx->scroll = 0;

    const EnemyType *enemies[MAX_BIOMES*MAX_BIOME_ROSTER];
    if (cx->tab==CODEX_ENEMIES) codex_gather_enemies(enemies, MAX_BIOMES*MAX_BIOME_ROSTER);

    for (int row=0; row<visible; row++) {
        int idx = cx->scroll + row; if (idx >= total) break;
        int y = top + row*rowH, x = 40, bw = SCREEN-80;
        DrawRectangle(x,y,bw,rowH-6, Fade(PAL_PANEL,0.6f));
        char sub[160];
        switch (cx->tab) {
            case CODEX_ENEMIES: { const EnemyType *e=enemies[idx];
                if (e->flavor)
                    snprintf(sub,sizeof(sub),"%s   (hp %.0f  spd %.0f  xp %.0f%s)", e->flavor, e->hp, e->speed, e->xp, e->elite?"  ELITE":"");
                else
                    snprintf(sub,sizeof(sub),"hp %.0f  spd %.0f  xp %.0f%s", e->hp, e->speed, e->xp, e->elite?"   [ELITE]":"");
                codex_row_ex(x,y, e->color, e->sprite, e->name?e->name:"?", sub); break; }
            case CODEX_WEAPONS: { const WeaponDef *d=&WEAPONS_DEF[idx];
                snprintf(sub,sizeof(sub),"dmg %.0f   interval %.2fs", d->baseDamage, d->baseInterval);
                codex_row_ex(x,y, d->color, d->sprite, d->name?d->name:"?", sub); break; }
            case CODEX_EFFECTS: { const EffectDef *e=&EFFECTS[idx];
                snprintf(sub,sizeof(sub),"%s%s%s dur %.1fs", e->isDot?"DoT  ":"", e->haltsMove?"halts  ":"", e->haltsFire?"silences  ":"", e->defDuration);
                codex_row_ex(x,y, e->tint, e->sprite, e->name?e->name:"?", sub); break; }
            case CODEX_PICKUPS:
                codex_row_ex(x,y, pickup_color(idx), pickup_sprite(idx), pickup_name(idx), pickup_desc(idx)); break;
            case CODEX_RUNES:
                if (idx < RUNE_COUNT) { const RuneDef *r=&RUNES[idx];
                    snprintf(sub,sizeof(sub),"rune  -  %s", r->stat?r->stat:"");
                    codex_row_ex(x,y, r->color, r->sprite, r->name?r->name:"?", sub); }
                else { const Trifecta *tr=&TRIFECTAS[idx-RUNE_COUNT];
                    snprintf(sub,sizeof(sub),"trifecta  R%d B%d G%d Y%d P%d", tr->counts[0],tr->counts[1],tr->counts[2],tr->counts[3],tr->counts[4]);
                    codex_row_ex(x,y, PAL_GOLD, tr->sprite, tr->name?tr->name:"?", sub); }
                break;
            case CODEX_DRONES: {
                const char *names[3]={"Trails you, fires an auto-aim turret","Roams to nearest foe, homing swarm","Sweeps the top, twin side guns"};
                codex_row_ex(x,y, PALETTE[PC_AQUABRT], drone_ai_sprite(idx), drone_ai_name(idx), names[idx%3]); break; }
            case CODEX_PASSIVES: { const Passive *p=&PASSIVES[idx];
                codex_row_ex(x,y, PAL_TEXT_DIM, p->sprite, p->name?p->name:"?", p->desc); break; }
            default: break;
        }
    }
    // scroll + nav hints
    if (total > visible) DrawText(TextFormat("%d-%d / %d", cx->scroll+1, (cx->scroll+visible<total?cx->scroll+visible:total), total),
                                  SCREEN-150, 78, 14, PAL_TEXT_DIM);
    const char *hint=pad_on()?"L/R bumpers: tab   -   D-pad: scroll   -   Square/B: back"
                             :"< > or A/D: tab    -    up/down: scroll    -    C or Esc: back";
    DrawText(hint,SCREEN/2-MeasureText(hint,16)/2,SCREEN-26,16,PAL_TEXT_DIM);
}

// ---- post-run results screen (replaces old survey) ----
// g_resultRank: set once when the results screen first shows (analytics_scores_submit called
// from game.c before render_results is first called). 0 = not top-10.
static int g_resultRank = 0;
void render_set_result_rank(int rank) { g_resultRank = rank; }

void render_survey(GameWorld *w) {   // kept as entry point; now draws the results screen
    DrawRectangle(0,0,SCREEN,SCREEN,Fade(BLACK,0.92f));
    Analytics *a = &w->analytics;
    float rt = (float)w->runTime;

    // ---- header ----
    const char *title = "RUN OVER";
    DrawText(title, SCREEN/2-MeasureText(title,40)/2, 10, 40, PALETTE[PC_CORAL]);
    // score + rank on same line
    char scoreBuf[64]; snprintf(scoreBuf,sizeof(scoreBuf),"%.0f pts", w->score);
    int scoreW = MeasureText(scoreBuf,26);
    DrawText(scoreBuf, SCREEN/2-scoreW/2, 54, 26, PAL_GOLD);
    if (g_resultRank > 0) {
        const char *rs = TextFormat("  #%d", g_resultRank);
        DrawText(rs, SCREEN/2+scoreW/2, 56, 20, PAL_GOOD);
    }
    // killed-by on its own line, dimmer
    if (a->causeOfDeath[0]) {
        const char *cod = TextFormat("killed by %s", a->causeOfDeath);
        DrawText(cod, SCREEN/2-MeasureText(cod,13)/2, 82, 13, Fade(PALETTE[PC_CORAL],0.7f));
    }

    // ---- divider ----
    DrawRectangle(14, 100, SCREEN-28, 1, Fade(PAL_TEXT_DIM, 0.35f));

    // ---- two-column body: y starts at 108 ----
    int lx=18, rx=SCREEN/2+8, body_y=108, dy=22;

    // LEFT: run stats
    {
        int cy = body_y;
        #define SROW(label, fmt, ...) do { \
            DrawText(label, lx, cy, 14, PAL_TEXT_DIM); \
            const char *_v = TextFormat(fmt, ##__VA_ARGS__); \
            DrawText(_v, lx+148-MeasureText(_v,14), cy, 14, RAYWHITE); \
            cy += dy; } while(0)
        DrawText("STATS", lx, cy, 11, Fade(PAL_TEXT_DIM,0.7f)); cy += 16;
        SROW("time",      "%.0fs",   w->runTime);
        SROW("stage",     "%d",      w->stage);
        SROW("level",     "%d",      w->player.level);
        SROW("kills",     "%d",      w->kills);
        SROW("hull lost", "%d",      a->hullLost);
        SROW("dodges",    "%d",      a->dodges);
        SROW("dmg out",   "%.0f",    a->damageDealt);
        SROW("gems",      "%d/%d",   a->gemsCollected, a->gemsDropped);
        SROW("shield avg","%.0f%%",  (a->shieldSamples>0 ? a->shieldPctSum/a->shieldSamples*100.0 : 0.0));
        SROW("biome",     "%s",      BIOMES[w->biome].name ? BIOMES[w->biome].name : "?");
        #undef SROW
    }

    // RIGHT: top-3 killer enemies
    {
        int nc = enemy_threat_count();
        int top3[3]={-1,-1,-1}; int top3dmg[3]={0,0,0};
        for (int i=0;i<nc && i<MAX_ENEMY_TYPES;i++) {
            int d = a->dmgToPlayerByType[i];
            for (int s=0;s<3;s++) {
                if (d > top3dmg[s]) {
                    for (int k=2;k>s;k--){top3[k]=top3[k-1];top3dmg[k]=top3dmg[k-1];}
                    top3[s]=i; top3dmg[s]=d; break;
                }
            }
        }
        int maxDmg = top3dmg[0]>0 ? top3dmg[0] : 1;

        DrawText("TOP KILLERS", rx, body_y, 11, Fade(PAL_TEXT_DIM,0.7f));

        // portrait size and row height
        int psz = 56;   // portrait box px
        int rowH = psz + 10;
        int barW = SCREEN - rx - psz - 24;

        for (int slot=0;slot<3;slot++) {
            int tid = top3[slot]; if (tid<0 || top3dmg[slot]==0) continue;
            const char *ename = enemy_threat_name(tid);

            EnemyType *etyp = NULL;
            for (int b=0;b<NUM_BIOMES && !etyp;b++)
                for (int i=0;i<BIOMES[b].rosterCount && !etyp;i++)
                    if (BIOMES[b].roster[i].threatId==tid) etyp=&BIOMES[b].roster[i];

            int ey = body_y + 18 + slot*rowH;
            Color rankCol = slot==0?PALETTE[PC_CORAL]:slot==1?PALETTE[PC_PEACH]:PAL_TEXT_DIM;

            // portrait: dark bg so shape reads, tinted border in rank color
            DrawRectangle(rx, ey, psz, psz, (Color){8,8,20,220});
            DrawRectangleLinesEx((Rectangle){rx,ey,psz,psz}, 2, Fade(rankCol,0.7f));
            if (etyp) {
                Vector2 ep = {rx + psz/2.0f, ey + psz/2.0f};
                float drawR = psz * 0.36f;  // fixed portrait radius regardless of in-game size
                if (!(etyp->sprite && sprite_draw(etyp->sprite, ep, drawR*2.4f, 0, WHITE)))
                    if (etyp->geo.count > 0) geometry_draw(&etyp->geo, ep, drawR, etyp->color, rt);
            }

            // text to the right of portrait
            int tx2 = rx + psz + 8;
            const char *rankStr = slot==0?"#1":slot==1?"#2":"#3";
            DrawText(rankStr, tx2, ey+2, 13, rankCol);
            DrawText(ename ? ename : "?", tx2, ey+16, 16, RAYWHITE);
            DrawText(TextFormat("%d hits", top3dmg[slot]), tx2, ey+34, 13, Fade(PAL_TEXT_DIM,0.9f));
            // bar under text
            DrawRectangle(tx2, ey+psz-10, barW, 6, Fade(PAL_PANEL,0.8f));
            DrawRectangle(tx2, ey+psz-10, (int)(barW*(float)top3dmg[slot]/maxDmg), 6, Fade(rankCol,0.85f));
        }
        if (top3[0]<0) DrawText("no data", rx+4, body_y+20, 14, PAL_TEXT_DIM);
    }

    // ---- divider ----
    int mid_y = body_y + 18 + 3*66 + 6;  // below 3 rows of killers (56px box + 10px gap)
    DrawRectangle(14, mid_y, SCREEN-28, 1, Fade(PAL_TEXT_DIM, 0.3f));
    int bot_y = mid_y + 8;

    // ---- bottom row: kill-zone histogram (left) | high scores (right) ----
    {
        // kill-zone histogram
        int hx=lx, hw=(SCREEN/2)-lx-8, hh=56;
        DrawText("KILL ZONE  top\xE2\x86\x92""btm", hx, bot_y, 11, Fade(PAL_TEXT_DIM,0.7f));
        int hy = bot_y + 16;
        int maxBand=1;
        for (int b=0;b<DEATH_Y_BANDS;b++) if (a->deathYBand[b]>maxBand) maxBand=a->deathYBand[b];
        int bw2 = hw/DEATH_Y_BANDS;
        for (int b=0;b<DEATH_Y_BANDS;b++) {
            int bx = hx + b*bw2;
            int barH2 = (int)((float)a->deathYBand[b]/maxBand * hh);
            DrawRectangle(bx, hy, bw2-2, hh, Fade(PAL_PANEL, 0.5f));
            if (barH2 > 0)
                DrawRectangle(bx, hy+hh-barH2, bw2-2, barH2, Fade(PALETTE[PC_LEAF],0.85f));
        }

        // stage timeline below histogram
        int tly = hy + hh + 14;
        DrawText("STAGE TIMELINE", hx, tly-12, 11, Fade(PAL_TEXT_DIM,0.7f));
        int tw2 = hw;
        if (a->stageRowCount > 0) {
            // only scale to stages cleared, not full run time
            float total = (float)w->runTime > 0 ? (float)w->runTime : 1;
            float xs = (float)tw2 / total;
            float t = 0;
            for (int i=0;i<a->stageRowCount && i<16;i++) {
                StageRow *sr = &a->stages[i];
                int bx2 = hx + (int)(t*xs);
                int bw3 = (int)(sr->clearTime*xs); if (bw3<3) bw3=3;
                Color sc = PALETTE[(PC_CORAL + i*3) % 32];
                DrawRectangle(bx2, tly, bw3-1, 16, Fade(sc,0.8f));
                if (bw3 > 20) DrawText(TextFormat("S%d",sr->stage), bx2+3, tly+2, 11, WHITE);
                t += sr->clearTime;
            }
            // remainder bar (time in current stage when died)
            int remX = hx+(int)(t*xs);
            if (remX < hx+tw2)
                DrawRectangle(remX, tly, hx+tw2-remX, 16, Fade(PAL_PANEL,0.6f));
        } else {
            DrawRectangle(hx, tly, tw2, 16, Fade(PAL_PANEL,0.5f));
            DrawText("no stages cleared", hx+4, tly+2, 11, PAL_TEXT_DIM);
        }
    }

    {
        // high scores — two-line layout per entry so nothing clips
        ScoreEntry scores[10]; int nscore = analytics_scores_load(scores);
        int hsx = rx, hsy = bot_y;
        int colW = SCREEN - hsx - 14;
        DrawText("HIGH SCORES", hsx, hsy, 11, Fade(PAL_TEXT_DIM,0.7f));
        hsy += 16;
        int show = nscore < 5 ? nscore : 5;
        int rowH2 = (SCREEN - 28 - hsy) / (show > 0 ? show : 1);
        if (rowH2 > 36) rowH2 = 36;
        for (int i=0;i<show;i++) {
            bool isMe = (g_resultRank == i+1);
            Color rc = isMe ? PAL_GOLD : (i==0 ? RAYWHITE : PAL_TEXT_DIM);
            int ey2 = hsy + i*rowH2;
            if (isMe) DrawRectangle(hsx-2, ey2-1, colW+2, rowH2-2, Fade(PAL_GOLD,0.08f));
            // line 1: rank + score
            DrawText(TextFormat("#%d  %.0f pts", i+1, scores[i].score), hsx, ey2, 14, rc);
            // line 2: stage / level / time / cause
            DrawText(TextFormat("  S%d L%d  %.0fs  %s", scores[i].stage, scores[i].level,
                scores[i].runTime, scores[i].cause), hsx, ey2+16, 12, Fade(rc,0.65f));
        }
        if (nscore == 0) DrawText("no scores yet", hsx+4, hsy+4, 13, PAL_TEXT_DIM);
    }

    // ---- footer ----
    const char *hint = pad_on() ? "Cross / Start to continue" : "Space / Enter to continue";
    DrawText(hint, SCREEN/2-MeasureText(hint,13)/2, SCREEN-16, 13, Fade(PAL_TEXT_DIM,0.7f));
}

void render_boss_warn(GameWorld *w) {
    (void)w;
    if (((int)(GetTime()*4))%2){ const char *m="!! WARNING !!";
        DrawText(m,SCREEN/2-MeasureText(m,40)/2,SCREEN/2-20,40,PALETTE[PC_PINKRED]); }
}

// stage-clear celebration: white flash + a slam banner that punches in oversized and settles
void render_stage_banner(GameWorld *w) {
    if (w->clearFlash > 0)
        DrawRectangle(0, 0, SCREEN, SCREEN, Fade(WHITE, w->clearFlash*0.55f));
    if (w->stageBannerTimer <= 0 || !w->stageBannerText) return;
    float shown = 2.4f - w->stageBannerTimer;             // time since the slam
    float a = fminf(1.0f, w->stageBannerTimer/0.45f);     // fade out at the end
    float punch = 1.0f + 1.4f*expf(-shown*9.0f);          // oversized -> settles fast
    int fs = (int)(52*punch);
    const char *txt = w->stageBannerText;
    int tw = MeasureText(txt, fs);
    // drop shadow + gold slam text
    DrawText(txt, SCREEN/2 - tw/2 + 3, (int)(SCREEN*0.34f) + 3, fs, Fade(BLACK, 0.6f*a));
    DrawText(txt, SCREEN/2 - tw/2, (int)(SCREEN*0.34f), fs, Fade(PAL_GOLD, a));
    if (w->stageBannerSub && shown > 0.35f) {
        char sub[96]; snprintf(sub, sizeof(sub), "NEXT: %s", w->stageBannerSub);
        float sa = a * fminf(1.0f, (shown-0.35f)/0.25f);
        DrawText(sub, SCREEN/2 - MeasureText(sub, 22)/2, (int)(SCREEN*0.34f) + fs + 10, 22,
                 Fade(PAL_TEXT, sa));
    }
}

void render_evo_banner(GameWorld *w) {
    if (w->evoBannerTimer <= 0 || !w->evoBannerName) return;
    float t = w->evoBannerTimer;
    float a = fminf(1.0f, t/0.4f) * fminf(1.0f, (3.5f-t)/0.3f + 1.0f);  // fade in/out
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    int by = 150;
    DrawRectangle(0, by-6, SCREEN, 86, Fade(PAL_PANEL, 0.82f*a));
    DrawRectangle(0, by-6, SCREEN, 3, Fade(PAL_GOLD, a));
    DrawRectangle(0, by+80, SCREEN, 3, Fade(PAL_GOLD, a));
    const char *tag = "WEAPON EVOLVED";
    DrawText(tag, SCREEN/2-MeasureText(tag,18)/2, by, 18, Fade(PAL_GOLD, a));
    DrawText(w->evoBannerName, SCREEN/2-MeasureText(w->evoBannerName,34)/2, by+22, 34, Fade(WHITE, a));
    DrawText(w->evoBannerDesc, SCREEN/2-MeasureText(w->evoBannerDesc,18)/2, by+60, 18, Fade(PAL_TEXT_DIM, a));
}
