// geoditor.c — interactive editor for Angerblast enemy shapes AND multi-part bosses.
// Build:  make geoditor (or: make bosseditor)     Run: ./build/geoditor [assets_dir]
//
// Two top-level modes (toolbar tabs):
//   SHAPES — the original per-enemy geometry layer editor.
//   BOSS   — compose a multi-part boss: parts (hp/fire/emitter/color), per-part
//            geometry (embedded shape editor), and keyframed motion with a timeline.
//
// Layout: [BROWSER 210] [PREVIEW flex] [RIGHT PANEL 300]  [TOML 36px bottom]
// Shapes keys: A=add  D/Del=delete  [/]=reorder  Tab=cycle op
//              Up/Dn=field  +/-/scroll=value  B=color  C=pal color  O=outline  S=copy
// Boss keys:   M=part/shape tab  Tab/1-5=part  [/]=reorder part  K=add keyframe
//              Del=del keyframe  Space=play/pause  Home=rewind  Lt/Rt=step (paused)
//              Esc=deselect keyframe  S=copy TOML

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "data/geometry.h"
#include "data/palette.h"
#include "data/biome.h"
#include "data/loader.h"
#include "data/enemy_types.h"
#include "data/emitter.h"
#include "data/anim.h"

// ---- layout (all sizes in pixels) ----
#define BROWSER_W    210
#define RIGHT_W      300
#define TOML_H        36
#define TOOLBAR_H     30
#define LAYER_ROW_H   30
#define LAYER_HDR_H   30
#define PART_ROW_H    26
#define FS            16   // base font size — minimum for raylib bitmap font

#define SW   GetScreenWidth()
#define SH   GetScreenHeight()
#define PRV_X   BROWSER_W
#define PRV_W   (SW - BROWSER_W - RIGHT_W)
#define PRV_H   (SH - TOML_H)
#define PRV_CX  (PRV_X + PRV_W/2)
#define PRV_CY  (TOOLBAR_H + (PRV_H - TOOLBAR_H)/2)
#define RP_X    (SW - RIGHT_W)
#define LAYER_LIST_H(n)  (LAYER_HDR_H + (n)*LAYER_ROW_H + 6)

// ---- colors ----
#define C_BG     (Color){ 10, 10, 18,255}
#define C_PANEL  (Color){ 20, 20, 36,255}
#define C_PANEL2 (Color){ 30, 30, 50,255}
#define C_BORDER (Color){ 55, 55, 90,255}
#define C_SEL    (Color){ 70,120,210,255}
#define C_TEXT   (Color){220,220,230,255}
#define C_DIM    (Color){110,110,140,255}
#define C_GOLD   (Color){255,200, 55,255}
#define C_GREEN  (Color){ 80,220,110,255}
#define C_RED    (Color){255, 70, 70,255}
#define C_PINK   (Color){230, 60,160,255}
#define C_CYAN   (Color){ 80,210,230,255}

// ---- geometry layer fields (shapes editor) ----
typedef enum {
    FLD_R=0, FLD_SIDES,
    FLD_W, FLD_H,
    FLD_THICK,
    FLD_X1, FLD_Y1, FLD_X2, FLD_Y2,
    FLD_SPIN, FLD_SPINOFF,
    FLD_ALPHA,
    FLD_COLREF, FLD_PALCOL,
    FLD_OUTLINE, FLD_OUTLINE_COL,
    FLD_COUNT
} FieldId;

static const char *FIELD_NAMES[FLD_COUNT] = {
    "r","sides","w","h","thick","x1","y1","x2","y2",
    "spin","spin_off","alpha",
    "color","pal_color",
    "outline","outline_col"
};

static const char *OP_NAMES[] = {"poly","circle","rect","ring","line"};
#define NUM_OPS 5

// which fields are relevant per op
static bool field_active(GeoOp op, FieldId f) {
    switch(op){
        case GOP_POLY:
            return f==FLD_R||f==FLD_SIDES||f==FLD_SPIN||f==FLD_SPINOFF
                ||f==FLD_ALPHA||f==FLD_COLREF||f==FLD_PALCOL
                ||f==FLD_OUTLINE||f==FLD_OUTLINE_COL;
        case GOP_CIRCLE:
            return f==FLD_R||f==FLD_ALPHA||f==FLD_COLREF||f==FLD_PALCOL;
        case GOP_RECT:
            return f==FLD_W||f==FLD_H||f==FLD_ALPHA||f==FLD_COLREF||f==FLD_PALCOL
                ||f==FLD_OUTLINE||f==FLD_OUTLINE_COL;
        case GOP_RING:
            return f==FLD_R||f==FLD_THICK||f==FLD_SPIN||f==FLD_SPINOFF
                ||f==FLD_ALPHA||f==FLD_COLREF||f==FLD_PALCOL;
        case GOP_LINE:
            return f==FLD_X1||f==FLD_Y1||f==FLD_X2||f==FLD_Y2
                ||f==FLD_THICK||f==FLD_ALPHA||f==FLD_COLREF||f==FLD_PALCOL;
        default: return false;
    }
}

// ---- boss part fields (boss editor, PART tab) ----
typedef enum {
    BF_HEAD=0, BF_OFFX, BF_OFFY, BF_HP, BF_RADIUS, BF_COLOR,
    BF_FIRE, BF_COUNT, BF_INTERVAL, BF_ARC, BF_SPIN, BF_AIMED,
    BF_SPEEDSC, BF_BCOLOR, BF_EMITTER,
    BF_COUNT_
} BossField;

static const char *BF_NAMES[BF_COUNT_] = {
    "head","off_x","off_y","hp","radius","color",
    "fire","count","interval","arc","spin","aimed",
    "speed_scale","bullet_col","emitter"
};

// ---- keyframe fields (boss editor, when a keyframe is selected) ----
typedef enum { KF_T=0, KF_X, KF_Y, KF_ROT, KF_SCALE, KF_TWEEN, KF_COUNT_ } KfField;
static const char *KF_NAMES[KF_COUNT_] = {"t","x","y","rot","scale","tween"};

// ---- state ----
static int   g_mode = 0;       // 0 = SHAPES (enemy geometry), 1 = BOSS

// shapes editor
static Geometry g_geo;
static int   g_selLayer = -1;
static int   g_selField = 0;
static Color g_bodyColor;
static float g_radius   = 32.0f;
static bool  g_darkBg   = true;
static float g_time     = 0;
static int   g_palIdx   = 0;   // current palette index for COLREF_PALETTE layers
static int   g_zoom     = 1;
static int   g_biome    = 0;
static int   g_enemy    = 0;
static char  g_enemyName[64];
static bool  g_dirty    = false;
static char  g_flash[80];
static float g_flashTimer = 0;

// boss editor
static BossPartDef g_parts[MAX_BOSS_PARTS];
static int   g_partCount = 0;
static char  g_bossName[64];
static float g_bossHp = 0, g_bossRadius = 0;
static int   g_selPart  = -1;
static int   g_selKf    = -1;   // keyframe index within the selected part, or -1
static int   g_bossTab  = 0;    // right panel: 0 = PART props, 1 = SHAPE (embedded geo editor)
static int   g_bField   = 1;    // selected row in the part/keyframe field list
static float g_animT    = 0;    // timeline scrub position (seconds)
static bool  g_playing  = true;
static bool  g_bossDirty = false;
static int   g_bZoom    = 1;
// interaction state
static int   g_dragPart = -1;          // canvas: part being dragged
static Vector2 g_dragGrab;             // canvas: mouse offset from part centre at grab
static int   g_tlDrag   = 0;           // timeline: 0 none, 1 scrub, 2 keyframe dot
static int   g_tlDragPart = -1, g_tlDragKf = -1;

// ---- UI helpers ----
static void panel(int x, int y, int w, int h){
    DrawRectangle(x,y,w,h,C_PANEL);
    DrawRectangleLinesEx((Rectangle){x,y,w,h},1,C_BORDER);
}

// button — returns true on click
static bool btn(int x, int y, int w, int h, const char *s, bool active){
    bool hov=CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,y,w,h});
    DrawRectangle(x,y,w,h, active?C_SEL:(hov?C_PANEL2:C_PANEL));
    DrawRectangleLinesEx((Rectangle){x,y,w,h},1, active?C_SEL:C_BORDER);
    int tw=MeasureText(s,FS);
    DrawText(s, x+(w-tw)/2, y+(h-FS)/2, FS, (active||hov)?C_TEXT:C_DIM);
    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

static bool clicked(int x, int y, int w, int h){
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
        && CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,y,w,h});
}

static void set_flash(const char *s){ snprintf(g_flash,sizeof(g_flash),"%s",s); g_flashTimer=2.f; }
static void mark_dirty(void){ if(g_mode==1) g_bossDirty=true; else g_dirty=true; }

// ---- palette helpers ----
static int pal_idx_of(Color c){   // exact registry match, or 0
    for(int p=0;p<palette_name_count();p++){
        Color n=PALETTE[palette_index_at(p)];
        if(n.r==c.r&&n.g==c.g&&n.b==c.b) return p;
    }
    return 0;
}
static const char *pal_name_of(Color c){
    const char *n=palette_name_at(pal_idx_of(c));
    return n?n:"white";
}
static Color pal_cycle(Color c, int dir){
    int n=palette_name_count(); if(n<=0) return c;
    int p=(pal_idx_of(c)+dir%n+n)%n;
    return PALETTE[palette_index_at(p)];
}

static Color colref_preview(ShapeLayer *L, Color body){
    if(L->colorRef==COLREF_BODY)  return body;
    if(L->colorRef==COLREF_WHITE) return WHITE;
    return L->paletteCol;
}

static const char *colref_str(ColorRef r){
    return r==COLREF_BODY?"body":r==COLREF_WHITE?"white":"palette";
}

// ---- geometry layer field get/set ----
static float get_field(ShapeLayer *L, FieldId f){
    switch(f){
        case FLD_R:          return L->r;
        case FLD_SIDES:      return (float)L->sides;
        case FLD_W:          return L->w;
        case FLD_H:          return L->h;
        case FLD_THICK:      return L->thick;
        case FLD_X1:         return L->x1;
        case FLD_Y1:         return L->y1;
        case FLD_X2:         return L->x2;
        case FLD_Y2:         return L->y2;
        case FLD_SPIN:       return L->spin;
        case FLD_SPINOFF:    return L->spinOffset;
        case FLD_ALPHA:      return L->alpha;
        case FLD_COLREF:     return (float)L->colorRef;
        case FLD_PALCOL:     return (float)g_palIdx;
        case FLD_OUTLINE:    return L->hasOutline?1.f:0.f;
        case FLD_OUTLINE_COL:return (float)L->outlineRef;
        default: return 0;
    }
}

static void set_field(ShapeLayer *L, FieldId f, float v){
    int n;
    switch(f){
        case FLD_R:       L->r         = fmaxf(0.01f,v);  break;
        case FLD_SIDES:   L->sides     = (int)fmaxf(3,v); break;
        case FLD_W:       L->w         = fmaxf(0.01f,v);  break;
        case FLD_H:       L->h         = fmaxf(0.01f,v);  break;
        case FLD_THICK:   L->thick     = fmaxf(0.01f,v);  break;
        case FLD_X1:      L->x1        = v; break;
        case FLD_Y1:      L->y1        = v; break;
        case FLD_X2:      L->x2        = v; break;
        case FLD_Y2:      L->y2        = v; break;
        case FLD_SPIN:    L->spin      = v; break;
        case FLD_SPINOFF: L->spinOffset= v; break;
        case FLD_ALPHA:   L->alpha     = Clamp(v,0.f,1.f); break;
        case FLD_COLREF: {
            L->colorRef=(ColorRef)(((int)v%3+3)%3);
            if(L->colorRef==COLREF_PALETTE && palette_name_count()>0)
                L->paletteCol=PALETTE[palette_index_at(g_palIdx)];
            break; }
        case FLD_PALCOL: {
            n=palette_name_count(); if(n<=0) break;
            g_palIdx=((int)v%n+n)%n;
            L->paletteCol=PALETTE[palette_index_at(g_palIdx)];
            break; }
        case FLD_OUTLINE:     L->hasOutline=(v>0.5f); break;
        case FLD_OUTLINE_COL: {
            // only body or white make sense for outlines
            int nr=((int)v%2+2)%2;
            L->outlineRef=(nr==0)?COLREF_BODY:COLREF_WHITE;
            break; }
        default: break;
    }
    mark_dirty();
}

static float field_step(FieldId f){
    switch(f){
        case FLD_SIDES:      return 1.f;
        case FLD_SPIN:
        case FLD_SPINOFF:    return 5.f;
        case FLD_COLREF:
        case FLD_PALCOL:
        case FLD_OUTLINE:
        case FLD_OUTLINE_COL:return 1.f;
        default:             return 0.05f;
    }
}

// ---- default layer ----
static ShapeLayer new_layer(GeoOp op){
    ShapeLayer L={0};
    L.op=op; L.r=1.f; L.w=1.f; L.h=1.f; L.thick=0.15f;
    L.x1=-1.f; L.x2=1.f; L.sides=5; L.alpha=1.f;
    L.colorRef=COLREF_BODY; L.outlineRef=COLREF_WHITE;
    return L;
}

// ---- load enemy ----
static void load_enemy(int b, int e){
    if(b<0||b>=NUM_BIOMES) return;
    Biome *bm=&BIOMES[b];
    if(e<0||e>=bm->rosterCount) return;
    EnemyType *et=&bm->roster[e];
    g_geo=et->geo;
    g_bodyColor=et->color;
    g_selLayer=(g_geo.count>0)?0:-1;
    g_selField=0;
    g_dirty=false;
    snprintf(g_enemyName,sizeof(g_enemyName),"%s",et->name?et->name:"?");
    // sync palette index to first palette-colored layer
    for(int i=0;i<g_geo.count;i++){
        if(g_geo.layers[i].colorRef==COLREF_PALETTE){
            g_palIdx=pal_idx_of(g_geo.layers[i].paletteCol);
            break;
        }
    }
}

// ---- load boss (copy the biome's BossDef into the editable state) ----
static void load_boss(int b){
    if(b<0||b>=NUM_BIOMES) return;
    BossDef *bd=&BIOMES[b].boss;
    snprintf(g_bossName,sizeof(g_bossName),"%s",bd->name?bd->name:"?");
    g_bossHp=bd->hp; g_bossRadius=bd->radius;
    g_partCount=bd->partCount;
    for(int i=0;i<g_partCount;i++) g_parts[i]=bd->parts[i];
    g_selPart=(g_partCount>0)?0:-1;
    g_selKf=-1; g_selLayer=-1; g_bField=1;
    g_animT=0; g_bossDirty=false;
}

static BossPartDef default_part(void){
    BossPartDef P; memset(&P,0,sizeof(P));
    P.offY=40; P.hp=300; P.radius=28; P.interval=1.2f;
    P.fireOp=-1; P.emitter=-1;
    P.color=palette_by_name("crimson");
    P.fp.count=8; P.fp.speedScale=1.25f; P.fp.arc=2*PI; P.fp.spin=0.3f;
    P.fp.color=palette_by_name("coral");
    return P;
}

// ---- keyframe ops (frames kept sorted by t) ----
static int kf_resort(BossAnimSeq *a, int idx){   // bubble frame idx into place, return new idx
    BossKeyframe t;
    while(idx>0 && a->frames[idx].t < a->frames[idx-1].t){
        t=a->frames[idx-1]; a->frames[idx-1]=a->frames[idx]; a->frames[idx]=t; idx--; }
    while(idx<a->frameCount-1 && a->frames[idx].t > a->frames[idx+1].t){
        t=a->frames[idx+1]; a->frames[idx+1]=a->frames[idx]; a->frames[idx]=t; idx++; }
    return idx;
}

// insert a keyframe at time t capturing the part's pose there; returns its index or -1
static int kf_add(BossPartDef *P, float t){
    BossAnimSeq *a=&P->anim;
    if(a->frameCount>=MAX_BOSS_KEYFRAMES) return -1;
    BossKeyframe k={0};
    k.t=t; k.offX=P->offX; k.offY=P->offY; k.scale=1.f; k.tween=TWEEN_EASE_INOUT;
    float rot=0,sc=1;
    anim_eval(a,t,&k.offX,&k.offY,&rot,&sc);
    k.rotation=rot; k.scale=sc;
    if(a->frameCount==0) a->loops=true;   // first keyframe: looping is the common case
    int idx=a->frameCount;
    a->frames[a->frameCount++]=k;
    g_bossDirty=true;
    return kf_resort(a,idx);
}

static void kf_delete(BossPartDef *P, int idx){
    BossAnimSeq *a=&P->anim;
    if(idx<0||idx>=a->frameCount) return;
    for(int i=idx;i<a->frameCount-1;i++) a->frames[i]=a->frames[i+1];
    a->frameCount--;
    g_bossDirty=true;
}

// timeline view length: longest sequence, padded, never below 4s
static float boss_timeline_len(void){
    float d=0;
    for(int i=0;i<g_partCount;i++){
        float sd=anim_seq_duration(&g_parts[i].anim);
        if(sd>d) d=sd;
    }
    return fmaxf(4.f, ceilf(d+0.999f));
}

// ---- boss part field get/set ----
static float bf_get(BossPartDef *P, int f){
    switch(f){
        case BF_HEAD:     return P->isHead?1.f:0.f;
        case BF_OFFX:     return P->offX;
        case BF_OFFY:     return P->offY;
        case BF_HP:       return P->hp;
        case BF_RADIUS:   return P->radius;
        case BF_COLOR:    return (float)pal_idx_of(P->color);
        case BF_FIRE:     return P->fireOp>=0?1.f:0.f;
        case BF_COUNT:    return (float)P->fp.count;
        case BF_INTERVAL: return P->interval;
        case BF_ARC:      return P->fp.arc;
        case BF_SPIN:     return P->fp.spin;
        case BF_AIMED:    return P->fp.aimed?1.f:0.f;
        case BF_SPEEDSC:  return P->fp.speedScale;
        case BF_BCOLOR:   return (float)pal_idx_of(P->fp.color);
        case BF_EMITTER:  return (float)P->emitter;
        default: return 0;
    }
}

static void bf_set(BossPartDef *P, int f, float v){
    int n;
    switch(f){
        case BF_HEAD:
            P->isHead=(v>0.5f);
            if(P->isHead) for(int i=0;i<g_partCount;i++)          // only one head
                if(&g_parts[i]!=P) g_parts[i].isHead=false;
            break;
        case BF_OFFX:     P->offX=v; break;
        case BF_OFFY:     P->offY=v; break;
        case BF_HP:       P->hp=fmaxf(1,v); break;
        case BF_RADIUS:   P->radius=fmaxf(4,v); break;
        case BF_COLOR: {
            n=palette_name_count(); if(n<=0) break;
            P->color=PALETTE[palette_index_at(((int)v%n+n)%n)];
            break; }
        case BF_FIRE:     P->fireOp=(v>0.5f)?FR_RADIAL:-1; break;
        case BF_COUNT:    P->fp.count=(int)fmaxf(1,v); break;
        case BF_INTERVAL: P->interval=fmaxf(0.1f,v); break;
        case BF_ARC:      P->fp.arc=Clamp(v,0.f,2*PI); break;
        case BF_SPIN:     P->fp.spin=v; break;
        case BF_AIMED:    P->fp.aimed=(v>0.5f); break;
        case BF_SPEEDSC:  P->fp.speedScale=fmaxf(0.05f,v); break;
        case BF_BCOLOR: {
            n=palette_name_count(); if(n<=0) break;
            P->fp.color=PALETTE[palette_index_at(((int)v%n+n)%n)];
            break; }
        case BF_EMITTER: {                       // wraps through -1 (none) .. NUM_EMITTERS-1
            n=NUM_EMITTERS+1;
            P->emitter=(((int)v+1)%n+n)%n-1;
            break; }
        default: break;
    }
    g_bossDirty=true;
}

static float bf_step(int f){
    switch(f){
        case BF_OFFX: case BF_OFFY: return 2.f;
        case BF_HP:                 return 50.f;
        case BF_RADIUS:             return 1.f;
        case BF_COUNT:              return 1.f;
        case BF_INTERVAL:           return 0.1f;
        case BF_ARC:                return 0.1f;
        case BF_SPIN:               return 0.05f;
        case BF_SPEEDSC:            return 0.05f;
        default:                    return 1.f;   // toggles / cycles
    }
}
static bool bf_is_enum(int f){
    return f==BF_HEAD||f==BF_COLOR||f==BF_FIRE||f==BF_AIMED||f==BF_BCOLOR||f==BF_EMITTER;
}

// ---- keyframe field get/set ----
static float kff_get(BossKeyframe *K, int f){
    switch(f){
        case KF_T:     return K->t;
        case KF_X:     return K->offX;
        case KF_Y:     return K->offY;
        case KF_ROT:   return K->rotation;
        case KF_SCALE: return K->scale;
        case KF_TWEEN: return (float)K->tween;
        default: return 0;
    }
}
// returns the (possibly moved) keyframe index — editing t re-sorts the sequence
static int kff_set(BossPartDef *P, int idx, int f, float v){
    BossKeyframe *K=&P->anim.frames[idx];
    switch(f){
        case KF_T:     K->t=fmaxf(0,v); idx=kf_resort(&P->anim,idx); break;
        case KF_X:     K->offX=v; break;
        case KF_Y:     K->offY=v; break;
        case KF_ROT:   K->rotation=v; break;
        case KF_SCALE: K->scale=Clamp(v,0.05f,4.f); break;
        case KF_TWEEN: K->tween=(TweenType)(((int)v%TWEEN_COUNT+TWEEN_COUNT)%TWEEN_COUNT); break;
        default: break;
    }
    g_bossDirty=true;
    return idx;
}
static float kff_step(int f){
    switch(f){
        case KF_T:     return 0.1f;
        case KF_X: case KF_Y: return 2.f;
        case KF_ROT:   return 5.f;
        case KF_SCALE: return 0.05f;
        default:       return 1.f;
    }
}

// ---- TOML: one geometry as `shape=[...]` (shared by the enemy bar + boss parts) ----
static int geo_toml(char *out, int cap, const Geometry *geo){
    int pos=0;
    pos+=snprintf(out+pos,cap-pos,"shape=[");
    for(int i=0;i<geo->count && pos<cap;i++){
        const ShapeLayer *L=&geo->layers[i];
        if(i) pos+=snprintf(out+pos,cap-pos,",");
        pos+=snprintf(out+pos,cap-pos,"{op=\"%s\"",OP_NAMES[L->op]);
        switch(L->op){
            case GOP_POLY:
                pos+=snprintf(out+pos,cap-pos,",sides=%d,r=%.2f",L->sides,L->r);
                if(fabsf(L->spin)>0.01f)      pos+=snprintf(out+pos,cap-pos,",spin=%.1f",L->spin);
                if(fabsf(L->spinOffset)>0.1f) pos+=snprintf(out+pos,cap-pos,",spin_offset=%.0f",L->spinOffset);
                break;
            case GOP_CIRCLE:
                pos+=snprintf(out+pos,cap-pos,",r=%.2f",L->r); break;
            case GOP_RECT:
                pos+=snprintf(out+pos,cap-pos,",w=%.2f,h=%.2f",L->w,L->h); break;
            case GOP_RING:
                pos+=snprintf(out+pos,cap-pos,",r=%.2f,thick=%.2f",L->r,L->thick);
                if(fabsf(L->spin)>0.01f)      pos+=snprintf(out+pos,cap-pos,",spin=%.1f",L->spin);
                if(fabsf(L->spinOffset)>0.1f) pos+=snprintf(out+pos,cap-pos,",spin_offset=%.0f",L->spinOffset);
                break;
            case GOP_LINE:
                pos+=snprintf(out+pos,cap-pos,",x1=%.2f,y1=%.2f,x2=%.2f,y2=%.2f,thick=%.2f",
                              L->x1,L->y1,L->x2,L->y2,L->thick);
                break;
        }
        // fill color
        if(L->colorRef==COLREF_BODY)       pos+=snprintf(out+pos,cap-pos,",color=\"body\"");
        else if(L->colorRef==COLREF_WHITE)  pos+=snprintf(out+pos,cap-pos,",color=\"white\"");
        else pos+=snprintf(out+pos,cap-pos,",color=\"%s\"",pal_name_of(L->paletteCol));
        if(L->alpha<0.99f) pos+=snprintf(out+pos,cap-pos,",alpha=%.1f",L->alpha);
        if(L->hasOutline){
            const char *oc=(L->outlineRef==COLREF_WHITE)?"white":"body";
            pos+=snprintf(out+pos,cap-pos,",outline=\"%s\"",oc);
        }
        pos+=snprintf(out+pos,cap-pos,"}");
    }
    pos+=snprintf(out+pos,cap-pos,"]");
    return pos;
}

// ---- TOML: the full [boss] block (pastes into a biome.toml) ----
static void build_boss_toml(char *out, int cap){
    int pos=0;
    pos+=snprintf(out+pos,cap-pos,"[boss]\nname = \"%s\"\nhp = %.0f\nradius = %.0f\n",
                  g_bossName,g_bossHp,g_bossRadius);
    pos+=snprintf(out+pos,cap-pos,"# single-body `phases` are not edited by this tool — keep your existing lines.\n");
    for(int i=0;i<g_partCount && pos<cap;i++){
        BossPartDef *P=&g_parts[i];
        pos+=snprintf(out+pos,cap-pos,"\n[[boss.part]]\n");
        if(P->isHead) pos+=snprintf(out+pos,cap-pos,"head = true\n");
        pos+=snprintf(out+pos,cap-pos,"off_x = %.0f\noff_y = %.0f\nhp = %.0f\nradius = %.0f\n",
                      P->offX,P->offY,P->hp,P->radius);
        pos+=snprintf(out+pos,cap-pos,"interval = %.2f\ncolor = \"%s\"\n",
                      P->interval,pal_name_of(P->color));
        if(P->fireOp>=0){
            // "radial" + explicit fire_params round-trips exactly (no name inference needed)
            pos+=snprintf(out+pos,cap-pos,
                "fire = \"radial\"\nfire_params = { count = %d, speed_scale = %.2f, arc = %.2f, spin = %.2f, aimed = %s, color = \"%s\" }\n",
                P->fp.count,P->fp.speedScale,P->fp.arc,P->fp.spin,
                P->fp.aimed?"true":"false",pal_name_of(P->fp.color));
        }
        if(P->emitter>=0 && P->emitter<NUM_EMITTERS)
            pos+=snprintf(out+pos,cap-pos,"emitter = \"%s\"\n",EMITTERS[P->emitter].name);
        if(P->geo.count>0){
            pos+=geo_toml(out+pos,cap-pos,&P->geo);
            pos+=snprintf(out+pos,cap-pos,"\n");
        }
        if(P->anim.frameCount>0){
            pos+=snprintf(out+pos,cap-pos,"anim = [\n");
            for(int k=0;k<P->anim.frameCount;k++){
                BossKeyframe *K=&P->anim.frames[k];
                pos+=snprintf(out+pos,cap-pos,
                    "  {t=%.2f, x=%.0f, y=%.0f, rot=%.0f, scale=%.2f, tween=\"%s\"},\n",
                    K->t,K->offX,K->offY,K->rotation,K->scale,tween_name(K->tween));
            }
            pos+=snprintf(out+pos,cap-pos,"]\n");
            if(P->anim.loops)      pos+=snprintf(out+pos,cap-pos,"anim_loops = true\n");
            if(P->anim.duration>0) pos+=snprintf(out+pos,cap-pos,"anim_duration = %.2f\n",P->anim.duration);
        }
    }
}

// enemy bar serializer (the original one-liner)
static void build_toml(char *out, int cap){ geo_toml(out,cap,&g_geo); }

// ============================================================
// DRAW — browser (left panel): biomes, then roster / boss summary
// ============================================================
static void draw_browser(void){
    int x=0,w=BROWSER_W,h=SH-TOML_H;
    panel(x,0,w,h);
    DrawText(g_mode==0?"ENEMIES":"BOSSES",x+8,8,FS,C_GOLD);

    int cy=28;
    for(int b=0;b<NUM_BIOMES;b++){
        bool sel=(b==g_biome);
        if(sel) DrawRectangle(x,cy,w,20,Fade(C_SEL,0.35f));
        else if(CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,cy,w,20}))
            DrawRectangle(x,cy,w,20,Fade(C_PANEL2,0.8f));
        DrawText(BIOMES[b].name?BIOMES[b].name:"?",x+8,cy+2,FS,sel?C_GOLD:C_TEXT);
        // boss-mode: flag biomes that already have a multi-part boss
        if(g_mode==1 && BIOMES[b].boss.partCount>0)
            DrawText(TextFormat("%d",BIOMES[b].boss.partCount),x+w-18,cy+2,FS,C_CYAN);
        DrawLine(x,cy+20,x+w,cy+20,C_BORDER);
        if(clicked(x,cy,w,20)){
            g_biome=b;
            if(g_mode==0){g_enemy=0;load_enemy(b,0);}
            else load_boss(b);
        }
        cy+=21;
    }
    cy+=4; DrawLine(x,cy,x+w,cy,Fade(C_BORDER,0.8f)); cy+=4;

    if(g_mode==0 && g_biome>=0&&g_biome<NUM_BIOMES){
        Biome *bm=&BIOMES[g_biome];
        for(int e=0;e<bm->rosterCount;e++){
            if(cy+20>h-2) break;
            bool sel=(e==g_enemy);
            if(sel) DrawRectangle(x,cy,w,20,Fade(C_SEL,0.3f));
            else if(CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,cy,w,20}))
                DrawRectangle(x,cy,w,20,Fade(C_PANEL2,0.6f));
            const char *nm=bm->roster[e].name?bm->roster[e].name:"?";
            DrawText(nm,x+8,cy+2,FS,sel?C_TEXT:C_DIM);
            DrawCircleV((Vector2){x+w-14,cy+10},5,bm->roster[e].color);
            if(clicked(x,cy,w,20)){g_enemy=e;load_enemy(g_biome,e);}
            cy+=20;
        }
    } else if(g_mode==1){
        // summary of the loaded boss
        DrawText(g_bossName,x+8,cy,FS,C_TEXT); cy+=20;
        DrawText(TextFormat("hp %.0f  r %.0f",g_bossHp,g_bossRadius),x+8,cy,FS,C_DIM); cy+=20;
        DrawText(TextFormat("%d part%s",g_partCount,g_partCount==1?"":"s"),x+8,cy,FS,C_DIM); cy+=20;
        int kf=0; for(int i=0;i<g_partCount;i++) kf+=g_parts[i].anim.frameCount;
        DrawText(TextFormat("%d keyframe%s",kf,kf==1?"":"s"),x+8,cy,FS,C_DIM); cy+=24;
        DrawText("reload biome to",x+8,cy,FS-2,Fade(C_DIM,0.7f)); cy+=14;
        DrawText("discard edits",x+8,cy,FS-2,Fade(C_DIM,0.7f));
    }
}

// mode tabs shared by both toolbars; returns x after the tabs
static int draw_mode_tabs(int x){
    if(btn(x,4,64,22,"SHAPE",g_mode==0)&&g_mode!=0){g_mode=0;}
    if(btn(x+66,4,54,22,"BOSS",g_mode==1)&&g_mode!=1){g_mode=1;load_boss(g_biome);}
    return x+126;
}

// ============================================================
// DRAW — shapes preview (center, enemy mode)
// ============================================================
static void draw_preview(void){
    int x=PRV_X,w=PRV_W,h=PRV_H;
    Color bg=g_darkBg?(Color){8,8,18,255}:(Color){120,125,140,255};
    DrawRectangle(x,0,w,h,bg);

    // toolbar
    DrawRectangle(x,0,w,TOOLBAR_H,C_PANEL2);
    DrawLine(x,TOOLBAR_H,x+w,TOOLBAR_H,C_BORDER);

    int tx=draw_mode_tabs(x+4)+4;
    // zoom buttons
    static float zooms[]={16,32,52};
    const char *zn[]={"S","M","L"};
    for(int z=0;z<3;z++){
        if(btn(tx,4,26,22,zn[z],g_zoom==z)){g_zoom=z;g_radius=zooms[z];}
        tx+=28;
    }
    tx+=2;
    if(btn(tx,4,38,22,g_darkBg?"DRK":"LGT",false)) g_darkBg=!g_darkBg;
    tx+=42;

    // body color
    DrawText("body:",tx,7,FS,C_DIM); tx+=46;
    DrawRectangle(tx,5,20,20,g_bodyColor);
    DrawRectangleLinesEx((Rectangle){tx,5,20,20},1,C_BORDER);
    tx+=24;
    if(btn(tx,4,22,22,"<",false)) g_bodyColor=pal_cycle(g_bodyColor,-1);
    tx+=24;
    if(btn(tx,4,22,22,">",false)) g_bodyColor=pal_cycle(g_bodyColor,+1);
    tx+=26;
    {   // body color name
        const char *pn=pal_name_of(g_bodyColor);
        if(pn) DrawText(pn,tx,7,FS,Fade(C_TEXT,0.6f));
    }

    // enemy name right-aligned
    {
        char title[80]; snprintf(title,sizeof(title),"%s%s",g_enemyName,g_dirty?" *":"");
        int tw=MeasureText(title,FS);
        DrawText(title,x+w-tw-8,7,FS,g_dirty?C_PINK:C_DIM);
    }

    // canvas: crosshair + reference ring
    int cx=PRV_CX, cy=PRV_CY;
    DrawLine(x,cy,x+w,cy,Fade(C_BORDER,0.3f));
    DrawLine(cx,TOOLBAR_H,cx,h,Fade(C_BORDER,0.3f));
    DrawCircleLines(cx,cy,(int)g_radius,Fade(C_DIM,0.4f));
    DrawText(TextFormat("r=%.0f",g_radius),cx+4,cy-(int)g_radius-FS-2,FS,Fade(C_DIM,0.5f));

    // full shape
    geometry_draw(&g_geo,(Vector2){cx,cy},g_radius,g_bodyColor,g_time);

    // solo inset — selected layer alone, bottom-left
    if(g_selLayer>=0&&g_selLayer<g_geo.count){
        int ix=x+8, iy=h-80;
        DrawRectangle(ix,iy,72,72,(Color){10,10,28,220});
        DrawRectangleLinesEx((Rectangle){ix,iy,72,72},1,Fade(C_SEL,0.8f));
        Geometry solo; solo.count=1; solo.layers[0]=g_geo.layers[g_selLayer];
        geometry_draw(&solo,(Vector2){ix+36,iy+36},26,g_bodyColor,g_time);
        DrawText("solo",ix+3,iy+3,12,Fade(C_SEL,0.9f));
    }

    // help bar bottom of canvas
    DrawText("A:add  D:del  [/]:reorder  Tab:op  Up/Dn:field  +/-:value  B:color  C:palcol  O:outline  S:copy",
             x+4,h-FS-2,FS-2,Fade(C_DIM,0.6f));
}

// ============================================================
// DRAW — geometry layer list (parameterized; enemy mode + boss SHAPE tab)
// returns the y just below the list
// ============================================================
static int draw_layer_list(Geometry *geo, Color body, int x, int y, int w){
    int h=LAYER_LIST_H(geo->count);
    panel(x,y,w,h);
    DrawText("LAYERS",x+8,y+8,FS,C_GOLD);

    // Add/Delete in header
    if(btn(x+w-54,y+4,24,22,"A",false)||
       (IsKeyPressed(KEY_A)&&!IsKeyDown(KEY_LEFT_CONTROL)&&!IsKeyDown(KEY_RIGHT_CONTROL))){
        if(geo->count<MAX_SHAPE_LAYERS){
            geo->layers[geo->count]=new_layer(GOP_POLY);
            g_selLayer=geo->count++;
            mark_dirty();
        }
    }
    if(btn(x+w-28,y+4,24,22,"D",false)||IsKeyPressed(KEY_DELETE)){
        if(g_selLayer>=0&&g_selLayer<geo->count&&geo->count>0){
            for(int i=g_selLayer;i<geo->count-1;i++) geo->layers[i]=geo->layers[i+1];
            geo->count--;
            if(g_selLayer>=geo->count) g_selLayer=geo->count-1;
            mark_dirty();
        }
    }

    int ly=y+LAYER_HDR_H;
    for(int i=0;i<geo->count;i++){
        ShapeLayer *L=&geo->layers[i];
        bool sel=(i==g_selLayer);
        bool hov=CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,ly,w,LAYER_ROW_H});

        if(sel)       DrawRectangle(x,ly,w,LAYER_ROW_H,Fade(C_SEL,0.28f));
        else if(hov)  DrawRectangle(x,ly,w,LAYER_ROW_H,Fade(C_PANEL2,0.7f));
        DrawLine(x,ly+LAYER_ROW_H-1,x+w,ly+LAYER_ROW_H-1,C_BORDER);

        // op badge
        Color oc;
        switch(L->op){
            case GOP_POLY:  oc=(Color){100,180,255,255};break;
            case GOP_CIRCLE:oc=(Color){ 80,220,160,255};break;
            case GOP_RECT:  oc=(Color){255,170, 60,255};break;
            case GOP_RING:  oc=(Color){200, 80,255,255};break;
            default:        oc=(Color){255, 80, 80,255};break;
        }
        DrawRectangle(x+3,ly+5,44,LAYER_ROW_H-10,Fade(oc,0.2f));
        DrawText(OP_NAMES[L->op],x+5,ly+(LAYER_ROW_H-FS)/2,FS,oc);

        // color swatch (clickable — cycles colorRef)
        Color sc=colref_preview(L,body);
        int swx=x+50, swy=ly+(LAYER_ROW_H-18)/2;
        DrawRectangle(swx,swy,18,18,sc);
        DrawRectangleLinesEx((Rectangle){swx,swy,18,18},1,C_BORDER);
        if(clicked(swx,swy,18,18)){
            set_field(L,FLD_COLREF,get_field(L,FLD_COLREF)+1.f);
        }

        // size summary
        char info[24];
        if(L->op==GOP_POLY||L->op==GOP_CIRCLE||L->op==GOP_RING)
            snprintf(info,sizeof(info),"r%.2f",L->r);
        else if(L->op==GOP_RECT)
            snprintf(info,sizeof(info),"%.1fx%.1f",L->w,L->h);
        else snprintf(info,sizeof(info),"line");
        DrawText(info,x+72,ly+(LAYER_ROW_H-FS)/2,FS,sel?C_TEXT:C_DIM);

        // reorder buttons — only on selected row
        if(sel){
            if(btn(x+w-50,ly+3,22,LAYER_ROW_H-6,"^",false)||IsKeyPressed(KEY_LEFT_BRACKET)){
                if(i>0){ShapeLayer t=geo->layers[i-1];geo->layers[i-1]=*L;*L=t;g_selLayer--;mark_dirty();}
            }
            if(btn(x+w-26,ly+3,22,LAYER_ROW_H-6,"v",false)||IsKeyPressed(KEY_RIGHT_BRACKET)){
                if(i<geo->count-1){ShapeLayer t=geo->layers[i+1];geo->layers[i+1]=*L;*L=t;g_selLayer++;mark_dirty();}
            }
        }

        if(clicked(x,ly,w-54,LAYER_ROW_H)) g_selLayer=i;
        ly+=LAYER_ROW_H;
    }
    if(geo->count==0) DrawText("(no layers — press A)",x+10,y+LAYER_HDR_H+8,FS,C_DIM);
    if(geo->count>=MAX_SHAPE_LAYERS) DrawText("MAX 6",x+8,ly+4,FS,C_RED);
    return y+h;
}

// ============================================================
// DRAW — geometry property panel (parameterized like the layer list)
// ============================================================
static void draw_geo_props(Geometry *geo, Color body, int x, int py, int w, int ph){
    if(ph<40) return;
    panel(x,py,w,ph);
    DrawText("PROPERTIES",x+8,py+8,FS,C_GOLD);

    if(g_selLayer<0||g_selLayer>=geo->count){
        DrawText("select a layer",x+10,py+30,FS,C_DIM);
        return;
    }
    ShapeLayer *L=&geo->layers[g_selLayer];
    int cy=py+30;

    // op type row — 5 small buttons
    int bw=(w-10)/NUM_OPS;
    for(int o=0;o<NUM_OPS;o++){
        if(btn(x+5+o*bw,cy,bw-2,24,OP_NAMES[o],(L->op==(GeoOp)o))){
            L->op=(GeoOp)o; mark_dirty();
            // snap selField to first active field for new op
            for(int fi=0;fi<FLD_COUNT;fi++){
                if(field_active(L->op,(FieldId)fi)){g_selField=fi;break;}
            }
        }
    }
    cy+=28;
    DrawLine(x,cy,x+w,cy,C_BORDER); cy+=4;

    // field rows
    int rowH=22;
    for(int fi=0;fi<FLD_COUNT&&cy+rowH<=py+ph-20;fi++){
        FieldId f=(FieldId)fi;
        bool active=field_active(L->op,f);

        // pal_color only interactive when color=palette
        bool interactive=active;
        if(f==FLD_PALCOL && L->colorRef!=COLREF_PALETTE) interactive=false;
        // outline_col only interactive when outline=yes
        if(f==FLD_OUTLINE_COL && !L->hasOutline) interactive=false;

        bool sel=(g_selField==fi);
        if(sel&&interactive) DrawRectangle(x,cy,w,rowH,Fade(C_SEL,0.22f));

        Color lc = sel&&interactive ? C_GOLD
                 : interactive      ? C_TEXT
                 : active           ? Fade(C_DIM,0.7f)
                 :                    Fade(C_DIM,0.35f);
        DrawText(FIELD_NAMES[fi],x+6,cy+3,FS,lc);

        // value text
        char vbuf[48]={0};
        if(f==FLD_COLREF)
            snprintf(vbuf,sizeof(vbuf),"%s",colref_str(L->colorRef));
        else if(f==FLD_PALCOL){
            if(L->colorRef==COLREF_PALETTE){
                const char *pn=palette_name_at(g_palIdx);
                snprintf(vbuf,sizeof(vbuf),"%s",pn?pn:"?");
            } else {
                snprintf(vbuf,sizeof(vbuf),"-");
            }
        }
        else if(f==FLD_OUTLINE)
            snprintf(vbuf,sizeof(vbuf),"%s",L->hasOutline?"yes":"no");
        else if(f==FLD_OUTLINE_COL)
            snprintf(vbuf,sizeof(vbuf),"%s",L->hasOutline?colref_str(L->outlineRef):"-");
        else if(f==FLD_SIDES)
            snprintf(vbuf,sizeof(vbuf),"%d",L->sides);
        else
            snprintf(vbuf,sizeof(vbuf),"%.2f",get_field(L,f));

        int vw=MeasureText(vbuf,FS);
        DrawText(vbuf,x+w-vw-8,cy+3,FS,lc);

        // palette swatch next to pal_color value
        if(f==FLD_PALCOL&&L->colorRef==COLREF_PALETTE){
            DrawRectangle(x+w-vw-30,cy+2,18,18,L->paletteCol);
            DrawRectangleLinesEx((Rectangle){x+w-vw-30,cy+2,18,18},1,C_BORDER);
        }

        // cycle buttons for enum fields, +/- for numeric — when selected & interactive
        if(sel&&interactive){
            bool isenum=(f==FLD_COLREF||f==FLD_OUTLINE||f==FLD_OUTLINE_COL);
            if(isenum){
                if(btn(x+w/2-24,cy,22,rowH-2,"<",false))
                    set_field(L,f,get_field(L,f)-1.f);
                if(btn(x+w/2,cy,22,rowH-2,">",false))
                    set_field(L,f,get_field(L,f)+1.f);
            } else {
                if(btn(x+w/2-24,cy,22,rowH-2,"-",false))
                    set_field(L,f,get_field(L,f)-field_step(f));
                if(btn(x+w/2,cy,22,rowH-2,"+",false))
                    set_field(L,f,get_field(L,f)+field_step(f));
            }
        }

        if(interactive&&clicked(x,cy,w,rowH)) g_selField=fi;
        cy+=rowH;
    }

    // hint
    DrawText("Up/Dn:field  +/-:value  B:color  C:palcol  O:outline",
             x+4,py+ph-FS-2,FS-2,Fade(C_DIM,0.55f));
    (void)body;
}

// ============================================================
// DRAW — TOML bar (bottom strip)
// ============================================================
static char g_bossToml[8192];   // built each frame in boss mode (also the copy source)

static void draw_toml_bar(void){
    int y=SH-TOML_H;
    DrawRectangle(0,y,SW,TOML_H,C_PANEL2);
    DrawLine(0,y,SW,y,C_BORDER);
    int bw=90;

    if(g_mode==0){
        static char toml[1200];
        build_toml(toml,sizeof(toml));
        BeginScissorMode(4,y,SW-bw-10,TOML_H);
        DrawText(toml,4,y+(TOML_H-FS)/2,FS,C_GREEN);
        EndScissorMode();
        if(btn(SW-bw-2,y+4,bw-2,TOML_H-8,"S  copy",false)||IsKeyPressed(KEY_S)){
            SetClipboardText(toml);
            set_flash("Copied!");
        }
    } else {
        // full boss block is many lines: show a summary, copy puts the block on the clipboard
        build_boss_toml(g_bossToml,sizeof(g_bossToml));
        int kf=0; for(int i=0;i<g_partCount;i++) kf+=g_parts[i].anim.frameCount;
        int lines=0; for(const char *c=g_bossToml;*c;c++) if(*c=='\n') lines++;
        DrawText(TextFormat("[boss] %s — %d parts, %d keyframes, %d TOML lines",
                 g_bossName,g_partCount,kf,lines),4,y+(TOML_H-FS)/2,FS,C_GREEN);
        if(btn(SW-bw-2,y+4,bw-2,TOML_H-8,"S  copy",false)||IsKeyPressed(KEY_S)){
            SetClipboardText(g_bossToml);
            set_flash("Boss TOML copied!");
        }
    }

    if(g_flashTimer>0){
        float a=fminf(1.f,g_flashTimer);
        int fw=MeasureText(g_flash,FS);
        DrawRectangle(SW/2-fw/2-6,y+2,fw+12,TOML_H-4,(Color){20,60,20,200});
        DrawText(g_flash,SW/2-fw/2,y+(TOML_H-FS)/2,FS,Fade(C_GREEN,a));
    }
}

// ============================================================
// BOSS — canvas helpers
// ============================================================
static float boss_zoom_scale(void){
    static const float zs[]={0.5f,0.8f,1.2f};
    return zs[g_bZoom];
}

// evaluated pose of part i at time t (base offsets when it has no keyframes)
static void part_pose(int i, float t, float *ox, float *oy, float *rot, float *sc){
    BossPartDef *P=&g_parts[i];
    *ox=P->offX; *oy=P->offY; *rot=0; *sc=1;
    anim_eval(&P->anim,t,ox,oy,rot,sc);
}

static Vector2 part_screen_pos(int i, float t){
    float ox,oy,rot,sc; part_pose(i,t,&ox,&oy,&rot,&sc);
    float s=boss_zoom_scale();
    return (Vector2){PRV_CX+ox*s, PRV_CY+oy*s};
}

// timeline geometry (shared by draw + input)
#define TL_ROW_H   16
#define TL_SCRUB_H 18
#define TL_LABEL_W 64
static int  tl_height(void){ return TL_SCRUB_H+TL_ROW_H*g_partCount+8; }
static int  tl_top(void){ return PRV_H-tl_height()-FS-6; }
static int  tl_track_x0(void){ return PRV_X+TL_LABEL_W; }
static int  tl_track_x1(void){ return PRV_X+PRV_W-12; }
static float tl_time_at(int mx){
    float u=(float)(mx-tl_track_x0())/(float)(tl_track_x1()-tl_track_x0());
    return Clamp(u,0.f,1.f)*boss_timeline_len();
}
static int tl_x_of(float t){
    return tl_track_x0()+(int)((t/boss_timeline_len())*(tl_track_x1()-tl_track_x0()));
}

// ============================================================
// DRAW — boss preview (center): toolbar, composed boss, timeline
// ============================================================
static void draw_boss_preview(void){
    int x=PRV_X,w=PRV_W,h=PRV_H;
    Color bg=g_darkBg?(Color){8,8,18,255}:(Color){120,125,140,255};
    DrawRectangle(x,0,w,h,bg);

    // toolbar
    DrawRectangle(x,0,w,TOOLBAR_H,C_PANEL2);
    DrawLine(x,TOOLBAR_H,x+w,TOOLBAR_H,C_BORDER);
    int tx=draw_mode_tabs(x+4)+4;
    const char *zn[]={"S","M","L"};
    for(int z=0;z<3;z++){ if(btn(tx,4,26,22,zn[z],g_bZoom==z)) g_bZoom=z; tx+=28; }
    tx+=2;
    if(btn(tx,4,38,22,g_darkBg?"DRK":"LGT",false)) g_darkBg=!g_darkBg;
    tx+=42;
    if(btn(tx,4,52,22,g_playing?"PAUSE":"PLAY",g_playing)) g_playing=!g_playing;
    tx+=56;
    DrawText(TextFormat("t=%.2f/%.1f",g_animT,boss_timeline_len()),tx,7,FS,C_DIM);
    {
        char title[96]; snprintf(title,sizeof(title),"%s%s",g_bossName,g_bossDirty?" *":"");
        int tw=MeasureText(title,FS);
        DrawText(title,x+w-tw-8,7,FS,g_bossDirty?C_PINK:C_DIM);
    }

    // canvas: anchor crosshair + arena reference
    float s=boss_zoom_scale();
    int cx=PRV_CX, cy=PRV_CY;
    DrawLine(x,cy,x+w,cy,Fade(C_BORDER,0.3f));
    DrawLine(cx,TOOLBAR_H,cx,tl_top()-2,Fade(C_BORDER,0.3f));
    DrawCircleLines(cx,cy,(int)(200*s),Fade(C_DIM,0.25f));
    DrawText("anchor",cx+6,cy+4,FS-2,Fade(C_DIM,0.5f));

    // tethers first (hands -> head), like the game render
    for(int i=0;i<g_partCount;i++){
        if(g_parts[i].isHead) continue;
        for(int hI=0;hI<g_partCount;hI++) if(g_parts[hI].isHead)
            DrawLineEx(part_screen_pos(i,g_animT),part_screen_pos(hI,g_animT),3*s,
                       Fade(C_DIM,0.35f));
    }

    // motion path ghost for the selected part
    if(g_selPart>=0&&g_selPart<g_partCount&&g_parts[g_selPart].anim.frameCount>1){
        BossAnimSeq *a=&g_parts[g_selPart].anim;
        float dur=anim_seq_duration(a);
        Vector2 prev=part_screen_pos(g_selPart,0);
        for(int k=1;k<=48;k++){
            Vector2 p=part_screen_pos(g_selPart,dur*k/48.f);
            DrawLineEx(prev,p,1,Fade(C_CYAN,0.35f));
            prev=p;
        }
        // clickable keyframe dots along the path
        for(int k=0;k<a->frameCount;k++){
            Vector2 kp={cx+a->frames[k].offX*s, cy+a->frames[k].offY*s};
            bool sel=(k==g_selKf);
            DrawCircleV(kp,sel?5:4,sel?C_GOLD:Fade(C_CYAN,0.8f));
            if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&
               CheckCollisionPointCircle(GetMousePosition(),kp,7)&&
               GetMousePosition().y<tl_top()) g_selKf=k;
        }
    }

    // parts
    bool shapeTab=(g_bossTab==1);
    for(int i=0;i<g_partCount;i++){
        BossPartDef *P=&g_parts[i];
        float ox,oy,rot,sc; part_pose(i,g_animT,&ox,&oy,&rot,&sc);
        Vector2 pp={cx+ox*s, cy+oy*s};
        float pr=P->radius*sc*s;
        bool sel=(i==g_selPart);

        if(shapeTab&&!sel){   // shape tab: spatial context only
            DrawCircleLines((int)pp.x,(int)pp.y,pr,Fade(C_DIM,0.4f));
            continue;
        }
        if(P->geo.count>0){
            rlPushMatrix();
            rlTranslatef(pp.x,pp.y,0);
            rlRotatef(rot,0,0,1);
            geometry_draw(&P->geo,(Vector2){0,0},pr,P->color,g_time);
            rlPopMatrix();
        } else {
            DrawCircleV(pp,pr,P->color);
            DrawCircleV(pp,pr*0.6f,Fade(WHITE,0.5f));
        }
        DrawCircleLines((int)pp.x,(int)pp.y,pr,Fade(sel?C_GOLD:C_DIM,sel?0.9f:0.35f));
        if(P->isHead) DrawText("H",(int)pp.x-4,(int)(pp.y-pr)-FS-2,FS,C_GOLD);
        DrawText(TextFormat("%d",i+1),(int)(pp.x+pr)-2,(int)(pp.y-pr)-2,FS-2,Fade(sel?C_GOLD:C_DIM,0.8f));
    }
    if(g_partCount==0)
        DrawText("no parts — press + in the PARTS panel",cx-140,cy-8,FS,C_DIM);

    // ---- timeline strip ----
    int ty=tl_top();
    DrawRectangle(x,ty,w,tl_height(),(Color){14,14,26,235});
    DrawLine(x,ty,x+w,ty,C_BORDER);
    int tx0=tl_track_x0(), tx1=tl_track_x1();
    float tlen=boss_timeline_len();

    // scrub bar with second ticks
    int sy=ty+2;
    DrawRectangle(tx0,sy+5,tx1-tx0,8,C_PANEL2);
    for(int sec=0;sec<=(int)tlen;sec++){
        int sx=tl_x_of((float)sec);
        DrawLine(sx,sy+3,sx,sy+15,Fade(C_DIM,0.5f));
        DrawText(TextFormat("%d",sec),sx+2,sy+1,FS-4,Fade(C_DIM,0.6f));
    }
    DrawText("time",x+6,sy+2,FS-2,C_DIM);

    // per-part rows
    for(int i=0;i<g_partCount;i++){
        BossPartDef *P=&g_parts[i];
        int ry=ty+TL_SCRUB_H+i*TL_ROW_H;
        bool psel=(i==g_selPart);
        if(psel) DrawRectangle(x,ry,w,TL_ROW_H,Fade(C_SEL,0.15f));
        DrawText(TextFormat("P%d%s",i+1,P->isHead?" H":""),x+6,ry+1,FS-2,psel?C_GOLD:C_DIM);
        // loop toggle
        bool lp=P->anim.loops;
        if(btn(x+TL_LABEL_W-24,ry+1,20,TL_ROW_H-3,"L",lp)){P->anim.loops=!lp;g_bossDirty=true;}
        DrawLine(tx0,ry+TL_ROW_H/2,tx1,ry+TL_ROW_H/2,Fade(C_BORDER,0.8f));
        // keyframe dots
        for(int k=0;k<P->anim.frameCount;k++){
            int kx=tl_x_of(P->anim.frames[k].t);
            bool ks=psel&&(k==g_selKf);
            DrawCircleV((Vector2){(float)kx,(float)(ry+TL_ROW_H/2)},ks?5:4,ks?C_GOLD:C_CYAN);
        }
        // sequence end marker
        if(P->anim.frameCount>0){
            int ex=tl_x_of(anim_seq_duration(&P->anim));
            DrawLine(ex,ry+2,ex,ry+TL_ROW_H-2,Fade(C_CYAN,0.4f));
        }
    }

    // time cursor across all rows
    int cxr=tl_x_of(g_animT);
    DrawLine(cxr,ty+4,cxr,ty+tl_height()-4,C_PINK);

    // help bar
    DrawText("drag part  K:keyframe  Del:del kf  Space:play  Home:rewind  M:part/shape  Tab/1-5:part  S:copy TOML",
             x+4,h-FS-2,FS-2,Fade(C_DIM,0.6f));
}

// ============================================================
// DRAW — boss part list (right panel top)
// ============================================================
static int draw_part_list(void){
    int x=RP_X,w=RIGHT_W;
    int h=LAYER_HDR_H+g_partCount*PART_ROW_H+6;
    panel(x,0,w,h);
    DrawText("PARTS",x+8,8,FS,C_GOLD);

    // add / duplicate / delete
    if(btn(x+w-80,4,24,22,"+",false)&&g_partCount<MAX_BOSS_PARTS){
        g_parts[g_partCount]=default_part();
        if(g_partCount==0) g_parts[0].isHead=true;   // first part defaults to the head
        g_selPart=g_partCount++;
        g_selKf=-1; g_bossDirty=true;
    }
    if(btn(x+w-54,4,24,22,"D",false)&&g_selPart>=0&&g_partCount<MAX_BOSS_PARTS){
        g_parts[g_partCount]=g_parts[g_selPart];
        g_parts[g_partCount].isHead=false;
        g_parts[g_partCount].offX+=30;
        g_selPart=g_partCount++;
        g_selKf=-1; g_bossDirty=true;
    }
    if(btn(x+w-28,4,24,22,"x",false)&&g_selPart>=0&&g_selPart<g_partCount){
        for(int i=g_selPart;i<g_partCount-1;i++) g_parts[i]=g_parts[i+1];
        g_partCount--;
        if(g_selPart>=g_partCount) g_selPart=g_partCount-1;
        g_selKf=-1; g_bossDirty=true;
    }

    int ly=LAYER_HDR_H;
    for(int i=0;i<g_partCount;i++){
        BossPartDef *P=&g_parts[i];
        bool sel=(i==g_selPart);
        bool hov=CheckCollisionPointRec(GetMousePosition(),(Rectangle){x,ly,w,PART_ROW_H});
        if(sel)      DrawRectangle(x,ly,w,PART_ROW_H,Fade(C_SEL,0.28f));
        else if(hov) DrawRectangle(x,ly,w,PART_ROW_H,Fade(C_PANEL2,0.7f));
        DrawLine(x,ly+PART_ROW_H-1,x+w,ly+PART_ROW_H-1,C_BORDER);

        // color swatch (clickable — cycles the part color)
        int swy=ly+(PART_ROW_H-16)/2;
        DrawRectangle(x+5,swy,16,16,P->color);
        DrawRectangleLinesEx((Rectangle){x+5,swy,16,16},1,C_BORDER);
        if(clicked(x+5,swy,16,16)) bf_set(P,BF_COLOR,bf_get(P,BF_COLOR)+1);

        DrawText(TextFormat("part %d",i+1),x+28,ly+(PART_ROW_H-FS)/2,FS,sel?C_TEXT:C_DIM);
        if(P->isHead) DrawText("H",x+92,ly+(PART_ROW_H-FS)/2,FS,C_GOLD);
        const char *fire=P->emitter>=0?"emit":P->fireOp>=0?"fire":"-";
        DrawText(TextFormat("hp%.0f r%.0f %s kf%d",P->hp,P->radius,fire,P->anim.frameCount),
                 x+110,ly+(PART_ROW_H-FS)/2,FS-2,sel?C_TEXT:C_DIM);

        if(clicked(x,ly,w,PART_ROW_H)){g_selPart=i;g_selKf=-1;}
        ly+=PART_ROW_H;
    }
    if(g_partCount==0) DrawText("(no parts — press +)",x+10,LAYER_HDR_H+6,FS,C_DIM);
    if(g_partCount>=MAX_BOSS_PARTS) DrawText("MAX",x+8,6+FS,FS-4,C_RED);
    return h;
}

// ============================================================
// DRAW — boss right panel bottom: PART / SHAPE tabs
// ============================================================
static void draw_boss_panel(void){
    int x=RP_X,w=RIGHT_W;
    int py=draw_part_list();

    // tab row
    if(btn(x+4,py+3,(w-12)/2,22,"PART",g_bossTab==0)) g_bossTab=0;
    if(btn(x+8+(w-12)/2,py+3,(w-12)/2,22,"SHAPE",g_bossTab==1)) g_bossTab=1;
    py+=28;

    if(g_selPart<0||g_selPart>=g_partCount){
        panel(x,py,w,SH-TOML_H-py);
        DrawText("select a part",x+10,py+10,FS,C_DIM);
        return;
    }
    BossPartDef *P=&g_parts[g_selPart];

    if(g_bossTab==1){
        // embedded geometry editor scoped to this part
        int ly=draw_layer_list(&P->geo,P->color,x,py,w);
        draw_geo_props(&P->geo,P->color,x,ly,w,SH-TOML_H-ly);
        return;
    }

    // ---- PART tab: part fields, or the selected keyframe's fields ----
    int ph=SH-TOML_H-py;
    if(ph<40) return;
    panel(x,py,w,ph);
    bool kfMode=(g_selKf>=0&&g_selKf<P->anim.frameCount);
    DrawText(kfMode?TextFormat("KEYFRAME %d",g_selKf+1):TextFormat("PART %d",g_selPart+1),
             x+8,py+8,FS,C_GOLD);
    if(kfMode) DrawText("Esc: back",x+w-84,py+8,FS-2,C_DIM);

    int cy=py+30, rowH=22;
    int nf = kfMode?KF_COUNT_:BF_COUNT_;
    if(g_bField>=nf) g_bField=nf-1;

    for(int fi=0;fi<nf&&cy+rowH<=py+ph-20;fi++){
        bool sel=(g_bField==fi);
        // fire params only interactive when fire is on
        bool interactive=true;
        if(!kfMode&&(fi==BF_COUNT||fi==BF_ARC||fi==BF_SPIN||fi==BF_AIMED
                   ||fi==BF_SPEEDSC||fi==BF_BCOLOR))
            interactive=(P->fireOp>=0);

        if(sel&&interactive) DrawRectangle(x,cy,w,rowH,Fade(C_SEL,0.22f));
        Color lc = sel&&interactive?C_GOLD : interactive?C_TEXT : Fade(C_DIM,0.5f);
        DrawText(kfMode?KF_NAMES[fi]:BF_NAMES[fi],x+6,cy+3,FS,lc);

        // value string
        char vbuf[48];
        if(kfMode){
            BossKeyframe *K=&P->anim.frames[g_selKf];
            if(fi==KF_TWEEN) snprintf(vbuf,sizeof(vbuf),"%s",tween_name(K->tween));
            else snprintf(vbuf,sizeof(vbuf),"%.2f",kff_get(K,fi));
        } else {
            switch(fi){
                case BF_HEAD:   snprintf(vbuf,sizeof(vbuf),"%s",P->isHead?"yes":"no"); break;
                case BF_COLOR:  snprintf(vbuf,sizeof(vbuf),"%s",pal_name_of(P->color)); break;
                case BF_FIRE:   snprintf(vbuf,sizeof(vbuf),"%s",P->fireOp>=0?"radial":"none"); break;
                case BF_COUNT:  snprintf(vbuf,sizeof(vbuf),"%d",P->fp.count); break;
                case BF_AIMED:  snprintf(vbuf,sizeof(vbuf),"%s",P->fp.aimed?"yes":"no"); break;
                case BF_BCOLOR: snprintf(vbuf,sizeof(vbuf),"%s",pal_name_of(P->fp.color)); break;
                case BF_EMITTER:
                    snprintf(vbuf,sizeof(vbuf),"%s",
                        (P->emitter>=0&&P->emitter<NUM_EMITTERS)?EMITTERS[P->emitter].name:"none");
                    break;
                default: snprintf(vbuf,sizeof(vbuf),"%.2f",bf_get(P,fi)); break;
            }
        }
        int vw=MeasureText(vbuf,FS);
        DrawText(vbuf,x+w-vw-8,cy+3,FS,lc);

        // swatches for the two color fields
        if(!kfMode&&(fi==BF_COLOR||fi==BF_BCOLOR)){
            Color sc=(fi==BF_COLOR)?P->color:P->fp.color;
            DrawRectangle(x+w-vw-30,cy+2,18,18,sc);
            DrawRectangleLinesEx((Rectangle){x+w-vw-30,cy+2,18,18},1,C_BORDER);
        }

        if(sel&&interactive){
            bool isenum = kfMode?(fi==KF_TWEEN):bf_is_enum(fi);
            const char *lb=isenum?"<":"-", *rb=isenum?">":"+";
            float st = kfMode?kff_step(fi):bf_step(fi);
            if(btn(x+w/2-24,cy,22,rowH-2,lb,false)){
                if(kfMode) g_selKf=kff_set(P,g_selKf,fi,kff_get(&P->anim.frames[g_selKf],fi)-st);
                else bf_set(P,fi,bf_get(P,fi)-st);
            }
            if(btn(x+w/2,cy,22,rowH-2,rb,false)){
                if(kfMode) g_selKf=kff_set(P,g_selKf,fi,kff_get(&P->anim.frames[g_selKf],fi)+st);
                else bf_set(P,fi,bf_get(P,fi)+st);
            }
        }
        if(interactive&&clicked(x,cy,w,rowH)) g_bField=fi;
        cy+=rowH;
    }

    DrawText(kfMode?"Up/Dn:field  +/-:value  Del:delete"
                   :"Up/Dn:field  +/-:value  K:add keyframe",
             x+4,py+ph-FS-2,FS-2,Fade(C_DIM,0.55f));
}

// ============================================================
// INPUT — shapes mode
// ============================================================
static void handle_shape_keys(Geometry *geo, int rpTop){
    if(g_selLayer<0||g_selLayer>=geo->count) return;
    ShapeLayer *L=&geo->layers[g_selLayer];

    // Tab = cycle op, snap to first active field
    if(IsKeyPressed(KEY_TAB)){
        L->op=(GeoOp)((L->op+1)%NUM_OPS); mark_dirty();
        for(int fi=0;fi<FLD_COUNT;fi++){
            if(field_active(L->op,(FieldId)fi)){g_selField=fi;break;}
        }
    }

    // Up/Down — skip inactive fields
    if(IsKeyPressed(KEY_UP)){
        int f=g_selField;
        do { f=(f-1+FLD_COUNT)%FLD_COUNT; } while(f!=g_selField && !field_active(L->op,(FieldId)f));
        g_selField=f;
    }
    if(IsKeyPressed(KEY_DOWN)){
        int f=g_selField;
        do { f=(f+1)%FLD_COUNT; } while(f!=g_selField && !field_active(L->op,(FieldId)f));
        g_selField=f;
    }

    FieldId f=(FieldId)g_selField;
    bool active=field_active(L->op,f);
    float step=field_step(f);

    if(active){
        if(IsKeyPressed(KEY_EQUAL)||IsKeyPressed(KEY_KP_ADD))
            set_field(L,f,get_field(L,f)+step);
        if(IsKeyPressed(KEY_MINUS)||IsKeyPressed(KEY_KP_SUBTRACT))
            set_field(L,f,get_field(L,f)-step);
        // scroll wheel on right panel
        if(CheckCollisionPointRec(GetMousePosition(),(Rectangle){RP_X,rpTop,RIGHT_W,SH-TOML_H-rpTop})){
            float wh=GetMouseWheelMove();
            if(wh!=0) set_field(L,f,get_field(L,f)+wh*step);
        }
    }

    // B = cycle colorRef
    if(IsKeyPressed(KEY_B))
        set_field(L,FLD_COLREF,get_field(L,FLD_COLREF)+1.f);

    // C = cycle palette color (when in palette mode)
    if(IsKeyPressed(KEY_C)&&L->colorRef==COLREF_PALETTE)
        set_field(L,FLD_PALCOL,(float)(g_palIdx+1));

    // O = toggle outline
    if(IsKeyPressed(KEY_O))
        set_field(L,FLD_OUTLINE,L->hasOutline?0.f:1.f);
}

// ============================================================
// INPUT — boss mode
// ============================================================
static void handle_boss_input(void){
    float dt=GetFrameTime();
    float tlen=boss_timeline_len();
    Vector2 m=GetMousePosition();

    // playback
    if(IsKeyPressed(KEY_SPACE)) g_playing=!g_playing;
    if(IsKeyPressed(KEY_HOME)){g_animT=0;}
    if(g_playing){ g_animT+=dt; if(g_animT>tlen) g_animT=0; }
    else {
        if(IsKeyPressed(KEY_RIGHT)) g_animT=fminf(tlen,g_animT+1.f/60.f);
        if(IsKeyPressed(KEY_LEFT))  g_animT=fmaxf(0.f,g_animT-1.f/60.f);
    }

    // mode / selection keys
    if(IsKeyPressed(KEY_M)) g_bossTab=!g_bossTab;
    if(IsKeyPressed(KEY_ESCAPE)) g_selKf=-1;
    for(int k=0;k<5&&k<g_partCount;k++)
        if(IsKeyPressed(KEY_ONE+k)){g_selPart=k;g_selKf=-1;}

    bool shapeTab=(g_bossTab==1);
    BossPartDef *P=(g_selPart>=0&&g_selPart<g_partCount)?&g_parts[g_selPart]:NULL;

    if(shapeTab&&P){
        // Tab/A/Del/B/C/O/wheel all act on the part's geometry (list is drawn, so A/Del
        // are handled there); field keys route through the shared shape handler.
        int rpTop=LAYER_HDR_H+g_partCount*PART_ROW_H+6+28;
        handle_shape_keys(&P->geo,rpTop);
        return;
    }

    // ---- layout tab ----
    if(P&&IsKeyPressed(KEY_TAB)){g_selPart=(g_selPart+1)%g_partCount;g_selKf=-1;}
    if(!P&&IsKeyPressed(KEY_TAB)&&g_partCount>0){g_selPart=0;}

    // reorder parts
    if(P&&IsKeyPressed(KEY_LEFT_BRACKET)&&g_selPart>0){
        BossPartDef t=g_parts[g_selPart-1];g_parts[g_selPart-1]=*P;g_parts[g_selPart]=t;
        g_selPart--;g_bossDirty=true;
    }
    if(P&&IsKeyPressed(KEY_RIGHT_BRACKET)&&g_selPart<g_partCount-1){
        BossPartDef t=g_parts[g_selPart+1];g_parts[g_selPart+1]=*P;g_parts[g_selPart]=t;
        g_selPart++;g_bossDirty=true;
    }

    // keyframe add / delete
    if(P&&IsKeyPressed(KEY_K)){
        int k=kf_add(P,g_animT);
        if(k>=0){g_selKf=k;g_playing=false;}
        else set_flash("keyframe cap reached");
    }
    if(P&&IsKeyPressed(KEY_DELETE)&&g_selKf>=0){
        kf_delete(P,g_selKf);
        g_selKf=-1;
    }

    // field +/- and wheel (part or keyframe fields)
    if(P){
        bool kfMode=(g_selKf>=0&&g_selKf<P->anim.frameCount);
        int nf=kfMode?KF_COUNT_:BF_COUNT_;
        if(IsKeyPressed(KEY_UP))   g_bField=(g_bField-1+nf)%nf;
        if(IsKeyPressed(KEY_DOWN)) g_bField=(g_bField+1)%nf;
        float st=kfMode?kff_step(g_bField):bf_step(g_bField);
        float delta=0;
        if(IsKeyPressed(KEY_EQUAL)||IsKeyPressed(KEY_KP_ADD))      delta=+st;
        if(IsKeyPressed(KEY_MINUS)||IsKeyPressed(KEY_KP_SUBTRACT)) delta=-st;
        if(CheckCollisionPointRec(m,(Rectangle){RP_X,0,RIGHT_W,SH-TOML_H})){
            float wh=GetMouseWheelMove();
            if(wh!=0) delta=wh*st;
        }
        if(delta!=0){
            if(kfMode) g_selKf=kff_set(P,g_selKf,g_bField,
                                       kff_get(&P->anim.frames[g_selKf],g_bField)+delta);
            else bf_set(P,g_bField,bf_get(P,g_bField)+delta);
        }
    }

    // ---- timeline interaction ----
    int ty=tl_top();
    bool inTimeline=(m.x>=PRV_X&&m.x<PRV_X+PRV_W&&m.y>=ty&&m.y<ty+tl_height());
    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&inTimeline){
        if(m.y<ty+TL_SCRUB_H){ g_tlDrag=1; g_playing=false; }
        else {
            int row=(int)((m.y-ty-TL_SCRUB_H)/TL_ROW_H);
            if(row>=0&&row<g_partCount&&m.x>=tl_track_x0()-6){
                // dot hit?
                int hit=-1;
                for(int k=0;k<g_parts[row].anim.frameCount;k++){
                    int kx=tl_x_of(g_parts[row].anim.frames[k].t);
                    int ky=ty+TL_SCRUB_H+row*TL_ROW_H+TL_ROW_H/2;
                    if(CheckCollisionPointCircle(m,(Vector2){(float)kx,(float)ky},6)){hit=k;break;}
                }
                g_selPart=row;
                if(hit>=0){ g_selKf=hit; g_tlDrag=2; g_tlDragPart=row; g_tlDragKf=hit; g_playing=false; }
                else { g_selKf=kf_add(&g_parts[row],tl_time_at((int)m.x)); g_playing=false; }
            }
        }
    }
    // right-click deletes a dot
    if(IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)&&inTimeline&&m.y>=ty+TL_SCRUB_H){
        int row=(int)((m.y-ty-TL_SCRUB_H)/TL_ROW_H);
        if(row>=0&&row<g_partCount){
            for(int k=0;k<g_parts[row].anim.frameCount;k++){
                int kx=tl_x_of(g_parts[row].anim.frames[k].t);
                int ky=ty+TL_SCRUB_H+row*TL_ROW_H+TL_ROW_H/2;
                if(CheckCollisionPointCircle(m,(Vector2){(float)kx,(float)ky},6)){
                    kf_delete(&g_parts[row],k);
                    if(g_selPart==row&&g_selKf==k) g_selKf=-1;
                    break;
                }
            }
        }
    }
    if(g_tlDrag==1){
        g_animT=tl_time_at((int)m.x);
        if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) g_tlDrag=0;
    } else if(g_tlDrag==2){
        BossPartDef *DP=&g_parts[g_tlDragPart];
        if(g_tlDragKf>=0&&g_tlDragKf<DP->anim.frameCount){
            g_tlDragKf=kff_set(DP,g_tlDragKf,KF_T,tl_time_at((int)m.x));
            if(g_selPart==g_tlDragPart) g_selKf=g_tlDragKf;
        }
        if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) g_tlDrag=0;
    }

    // ---- canvas part dragging (layout tab, outside toolbar/timeline) ----
    bool inCanvas=(m.x>=PRV_X&&m.x<PRV_X+PRV_W&&m.y>=TOOLBAR_H&&m.y<ty);
    float s=boss_zoom_scale();
    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)&&inCanvas&&g_tlDrag==0){
        for(int i=g_partCount-1;i>=0;i--){
            Vector2 pp=part_screen_pos(i,g_animT);
            float ox2,oy2,rot,sc; part_pose(i,g_animT,&ox2,&oy2,&rot,&sc);
            if(CheckCollisionPointCircle(m,pp,fmaxf(10,g_parts[i].radius*sc*s))){
                g_selPart=i; g_dragPart=i;
                g_dragGrab=(Vector2){m.x-pp.x,m.y-pp.y};
                // dragging an animated part edits its nearest keyframe (auto-selected)
                BossAnimSeq *a=&g_parts[i].anim;
                if(a->frameCount>0){
                    int best=0; float bd=1e9f;
                    for(int k=0;k<a->frameCount;k++){
                        float d=fabsf(a->frames[k].t-g_animT);
                        if(d<bd){bd=d;best=k;}
                    }
                    g_selKf=best; g_playing=false;
                } else g_selKf=-1;
                break;
            }
        }
    }
    if(g_dragPart>=0){
        BossPartDef *DP=&g_parts[g_dragPart];
        float nx=(m.x-g_dragGrab.x-PRV_CX)/s;
        float ny=(m.y-g_dragGrab.y-PRV_CY)/s;
        if(g_selKf>=0&&g_selKf<DP->anim.frameCount){
            DP->anim.frames[g_selKf].offX=nx;
            DP->anim.frames[g_selKf].offY=ny;
        } else { DP->offX=nx; DP->offY=ny; }
        g_bossDirty=true;
        if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) g_dragPart=-1;
    }
}

static void handle_input(void){
    if(g_flashTimer>0) g_flashTimer-=GetFrameTime();
    if(IsKeyPressed(KEY_M)&&g_mode==0){g_mode=1;load_boss(g_biome);return;}
    if(g_mode==1){handle_boss_input();return;}
    handle_shape_keys(&g_geo,0);
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char **argv){
    const char *assets=(argc>1)?argv[1]:"assets";
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
    InitWindow(1150,760,"Angerblast — Geometry & Boss Editor");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // Esc deselects (boss mode); quit via the window close button

    if(!loader_load_all(assets)){
        TraceLog(LOG_ERROR,"could not load assets from: %s",assets);
        CloseWindow(); return 1;
    }

    g_bodyColor=PALETTE[PC_CORAL];
    load_enemy(0,0);
    load_boss(0);

    while(!WindowShouldClose()){
        g_time+=GetFrameTime();
        handle_input();

        BeginDrawing();
        ClearBackground(C_BG);

        if(g_mode==0){
            draw_preview();
            draw_browser();
            int ly=draw_layer_list(&g_geo,g_bodyColor,RP_X,0,RIGHT_W);
            draw_geo_props(&g_geo,g_bodyColor,RP_X,ly,RIGHT_W,SH-TOML_H-ly);
        } else {
            draw_boss_preview();
            draw_browser();
            draw_boss_panel();
        }
        draw_toml_bar();

        EndDrawing();
    }
    CloseWindow();
    return 0;
}
