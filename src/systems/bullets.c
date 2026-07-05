// bullets.c - bullet pool + grid-accelerated collision.
#include "systems/bullets.h"
#include "data/palette.h"
#include "systems/enemies.h"   // enemies_kill, enemies_nearest, apply_effect, enemy_incoming
#include "systems/fx.h"
#include "systems/gems.h"      // gems_magnetize_near (Prospector)
#include "systems/puddles.h"   // puddles_spawn (burn on-hit)
#include "systems/player.h"    // player_damage
#include "systems/emit.h"      // composable emitter graph (child bullets)
#include "platform/audio.h"    // splash boom
#include "systems/boss.h"      // boss_damage_part (multi-part bosses)
#include "data/evolutions.h"   // ONKILL_*
#include <math.h>

// run a bullet's on-kill behaviour at the death point (after enemies_kill)
static void bullet_onkill(GameWorld *w, Bullet *b, Vector2 pos, Color col, float maxHp, bool wasBurning) {
    if (b->magnetKill) gems_magnetize_near(w, pos, 220);
    switch (b->onKill) {
        case ONKILL_MAGNET:   gems_magnetize_near(w, pos, 220); break;
        case ONKILL_CHAIN:    enemies_damage_near(w, pos, 50, maxHp*0.5f);
                              fx_spawn_particles(w, pos, 12, PALETTE[PC_PEACH], 220); break;
        case ONKILL_SHIELD:   w->player.shield = fminf(w->player.shieldMax, w->player.shield + 1.5f); break;
        case ONKILL_BOUNTY:   gems_drop(w, pos, 1.0, col); break;
        case ONKILL_FRAG:     for (int k=0;k<3;k++){ float a=frand(w)*2*PI;
                                  bullets_add(w, pos,(Vector2){cosf(a)*140,sinf(a)*140}, 4, b->damage*0.4f, col, true, WK_HOMING, 0, true); } break;
        case ONKILL_REIGNITE: if (wasBurning) puddles_spawn(w, pos, 28, 2.0f, b->damage*0.5f); break;
        default: break;
    }
}

// trigger a bullet's on-hit effect at the strike point
static void bullet_onhit(GameWorld *w, Bullet *b) {
    if (b->onHit == HIT_SPLASH) {          // charged main-cannon shot: big pop
        enemies_damage_near(w, b->pos, b->splashR, b->damage * 0.7f);
        fx_spawn_ring(w, b->pos, 10, b->splashR, 0.28f, 3, PALETTE[PC_GOLD]);
        fx_spawn_particles(w, b->pos, 26, PALETTE[PC_PEACH], 260);
        w->shakeAmount = fmaxf(w->shakeAmount, 8);
        fx_hitstop(w, 0.05f);
        audio_play(&w->audio, SFX_EXPLODE);
    } else if (b->onHit == HIT_EXPLODE) {
        enemies_damage_near(w, b->pos, 42, b->damage * 0.6f);
        fx_spawn_particles(w, b->pos, 10, PALETTE[PC_PEACH], 200);
        w->shakeAmount = fmaxf(w->shakeAmount, 3);
    } else if (b->onHit == HIT_BURN) {
        puddles_spawn(w, b->pos, 30, 2.0f, b->damage * 0.5f);
        fx_spawn_particles(w, b->pos, 5, PALETTE[PC_PEACH], 120);
    }
}

int bullets_add(GameWorld *w, Vector2 pos, Vector2 vel, float radius, float dmg,
                Color color, bool friendly, int weaponKind, int pierce, bool homing) {
    BulletPool *bp = &w->bullets;
    int slot = -1;
    for (int i = 0; i < MAX_BULLETS; i++) {
        int idx = (bp->cursor + i) % MAX_BULLETS;
        if (!bp->items[idx].active) { slot = idx; bp->cursor = (idx + 1) % MAX_BULLETS; break; }
    }
    if (slot < 0) return -1;
    // friendly bullets pick up the weapon's current stamps (effects/style/etc)
    int   onHit  = friendly ? w->bulletOnHit     : HIT_NONE;
    int   style  = friendly ? w->bulletStyle     : BSTYLE_DOT;
    int   effect = friendly ? w->bulletEffect    : -1;
    float emag   = friendly ? w->bulletEffectMag : 0;
    bool  magK   = friendly ? w->bulletMagnetKill: false;
    int   onKill = friendly ? w->bulletOnKill    : 0;
    int   bounce = friendly ? w->bulletBounce    : 0;
    bool  crit   = friendly ? w->bulletCrit      : false;
    bool destroys = friendly ? w->bulletDestroysBullets : false;
    float splashR = friendly ? w->bulletSplashR : 0;
    short srcT = friendly ? -1 : w->bulletSrcType;   // which enemy type fired this (blame)
    bp->items[slot] = (Bullet){ .pos=pos, .vel=vel, .radius=radius, .life=6.0f, .active=true,
        .color=color, .friendly=friendly, .weaponKind=weaponKind, .damage=dmg,
        .piercing=(pierce>0), .pierceLeft=pierce, .homing=homing, .onHit=onHit, .style=style,
        .effect=effect, .effectMag=emag, .magnetKill=magK, .onKill=onKill, .bounceLeft=bounce, .crit=crit,
        .destroysBullets=destroys, .splashR=splashR, .childEmitter=-1, .childMode=CHILD_NONE, .srcType=srcT };
    if (friendly) w->analytics.bulletsFired++;
    else if (srcT >= 0 && srcT < MAX_ENEMY_TYPES) w->analytics.bulletsByType[srcT]++;  // per-type bullet count
    return slot;
}

void bullets_update(GameWorld *w, float dt) {
    Grid *grid = &w->grid;
    Boss *boss = &w->boss;
    Player *pl = &w->player;
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets.items[i]; if (!b->active) continue;
        if (b->homing) {
            Vector2 tp; bool isBoss; int ei; bool have = false;
            if (b->seek) {   // charged shot: chase the LOCKED target while it lives
                if (b->seekIsBoss) {
                    if (boss->active) {
                        if (b->seekIdx >= 0 && b->seekIdx < boss->partCount && boss->parts[b->seekIdx].alive) { tp = boss->parts[b->seekIdx].pos; have = true; }
                        else if (b->seekIdx < 0) { tp = boss->pos; have = true; }
                    }
                } else if (b->seekIdx >= 0 && w->enemies.items[b->seekIdx].active) {
                    tp = w->enemies.items[b->seekIdx].pos; have = true;
                }
                if (!have) b->seek = false;   // target died mid-flight: generic homing
            }
            if (!have) have = target_nearest(w, b->pos, 500, -1, false, &tp, &isBoss, &ei);
            if (have) {
                Vector2 want = Vector2Scale(Vector2Normalize(Vector2Subtract(tp, b->pos)), Vector2Length(b->vel));
                b->vel = Vector2Lerp(b->vel, want, b->seek ? 0.28f : 0.15f);
            }
        }
        b->pos = Vector2Add(b->pos, Vector2Scale(b->vel, dt)); b->life -= dt;
        if (b->childEmitter >= 0) emitter_bullet_tick(w, b, dt);   // composable sub-emitter
        bool oob = b->pos.x<-40||b->pos.x>SCREEN+40||b->pos.y<-40||b->pos.y>SCREEN+40;
        if (b->life<=0 || oob) {
            // Only fire death-burst if the bullet expired naturally (lifetime ran out), NOT if it
            // left the arena — so splitter darts that fly offscreen don't spawn edge-rings.
            if (b->chargeShot && !oob) bullet_onhit(w, b);   // short fuse: detonate, don't fizzle
            if (b->childEmitter >= 0 && !oob) emitter_bullet_death(w, b);
            b->active=false; continue; }

        if (b->friendly) {
            // Aegis: an Aegis-stamped shot can knock down ONE nearby enemy bullet, then loses
            // the property (so it's chip point-defense, not a screen-clearing forcefield).
            if (b->destroysBullets) {
                float dr = b->radius + 5;
                for (int j = 0; j < MAX_BULLETS; j++) { Bullet *eb = &w->bullets.items[j];
                    if (!eb->active || eb->friendly) continue;
                    if (Vector2DistanceSqr(b->pos, eb->pos) <= dr*dr) {
                        eb->active = false; b->destroysBullets = false;  // spent
                        fx_spawn_particles(w, eb->pos, 8, WHITE, 140);
                        fx_spawn_particles(w, eb->pos, 4, b->color, 80);
                        break; } }
            }
            int cx=(int)(b->pos.x/CELL_SIZE), cy=(int)(b->pos.y/CELL_SIZE); bool consumed=false;
            for (int gy=cy-1; gy<=cy+1 && !consumed; gy++){ if(gy<0||gy>=GRID_CELLS)continue;
                for (int gx=cx-1; gx<=cx+1 && !consumed; gx++){ if(gx<0||gx>=GRID_CELLS)continue;
                    int c=gy*GRID_CELLS+gx;
                    for (int k=0;k<grid->n[c];k++){ int ei=grid->cell[c][k]; Enemy *e=&w->enemies.items[ei]; if(!e->active)continue;
                        float rr=b->radius+e->radius;
                        if (Vector2DistanceSqr(b->pos,e->pos)<=rr*rr){
                            // Executioner: low-HP bonus; crit doubles; Weaken amplifies
                            float dmg = b->damage;
                            if (e->hp < e->maxHp*0.25f) dmg *= 1.5f;
                            if (b->crit) dmg *= 2.0f;
                            dmg = enemy_incoming(e, dmg);
                            e->hp-=dmg; fx_spawn_damagenum(w, e->pos,(int)(dmg+0.5f), b->crit);
                            fx_spawn_particles(w, e->pos,2,b->color,100);
                            e->hitFlash = 0.07f;                       // white damage blink
                            // analytics: hit, damage, crit, per-weapon-kind damage share
                            { Analytics *a=&w->analytics; a->bulletHits++; a->damageDealt += dmg;
                              if (b->weaponKind>=0 && b->weaponKind<8) a->dmgByWeaponKind[b->weaponKind]+=dmg;
                              if (b->crit) { a->critHits++; a->critDamage += dmg; } }
                            if(b->effect>=0) enemies_apply_effect(w, e, b->effect, 0, b->effectMag);
                            if(b->onHit) bullet_onhit(w, b);
                            if(e->hp<=0){ Vector2 dp=e->pos; Color dc=e->color; float mhp=e->maxHp;
                                bool wasBurning = e->effTimer[EFFECT_BURN] > 0;
                                enemies_kill(w, e);
                                bullet_onkill(w, b, dp, dc, mhp, wasBurning); }
                            if(b->bounceLeft>0){ int t=enemies_nearest(w,b->pos,300,ei);
                                if(t>=0){ float sp=Vector2Length(b->vel);
                                    b->vel=Vector2Scale(Vector2Normalize(Vector2Subtract(w->enemies.items[t].pos,b->pos)),sp);
                                    b->bounceLeft--; consumed=true; break; } }
                            if(b->pierceLeft>0) b->pierceLeft--; else { b->active=false; consumed=true; break; }
                        } } } }
            if (b->active && boss->active) {
                if (boss->partCount > 0) {                 // multi-part: hit the nearest alive part
                    for (int pi=0; pi<boss->partCount; pi++){ BossPartState *ps=&boss->parts[pi]; if(!ps->alive)continue;
                        float rr=b->radius+ps->radius;
                        if (Vector2DistanceSqr(b->pos,ps->pos)<=rr*rr){
                            if (boss_damage_part(w, pi, b->damage)) fx_spawn_damagenum(w, ps->pos,(int)b->damage,false);
                            if(b->pierceLeft>0) b->pierceLeft--; else b->active=false;
                            break; } }
                } else if (Vector2DistanceSqr(b->pos,boss->pos)<=(b->radius+boss->radius)*(b->radius+boss->radius)){
                    boss->hp-=b->damage; fx_spawn_damagenum(w, boss->pos,(int)b->damage,false);
                    if(b->pierceLeft>0) b->pierceLeft--; else b->active=false; }
            }
        } else {
            if (Vector2DistanceSqr(b->pos,pl->pos)<=(b->radius+pl->radius)*(b->radius+pl->radius)){
                b->active=false; w->analytics.enemyBulletHits++;
                if (b->srcType >= 0 && b->srcType < MAX_ENEMY_TYPES) w->analytics.dmgToPlayerByType[b->srcType]++;
                const char *nm = enemy_threat_name(b->srcType);   // srcType is the stable name-slot
                player_damage(w, 1, nm ? nm : "enemy bullet"); }
        }
    }
}
