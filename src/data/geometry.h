// geometry.h - data-driven enemy silhouettes. A shape is a list of primitive draw layers
// (poly/circle/rect/ring/line), each composed with params. All sizes are fractions of the
// entity's radius (1.0 = full radius). Replaces the hardcoded shape switch.
#ifndef ANGERBLAST_GEOMETRY_H
#define ANGERBLAST_GEOMETRY_H

#include "raylib.h"

#define MAX_SHAPE_LAYERS 6

typedef enum { GOP_POLY, GOP_CIRCLE, GOP_RECT, GOP_RING, GOP_LINE } GeoOp;

// "body" / "white" are special colour tokens resolved against the entity colour at draw.
typedef enum { COLREF_PALETTE, COLREF_BODY, COLREF_WHITE } ColorRef;

typedef struct {
    GeoOp    op;
    int      sides;        // poly: number of sides
    float    r;            // poly/circle/ring radius (fraction of entity radius)
    float    w, h;         // rect: half-extents... actually full w/h in radius units
    float    x1,y1,x2,y2;  // line endpoints (radius units, relative to centre)
    float    thick;        // ring/line thickness (radius units)
    float    spin;         // deg/sec animation
    float    spinOffset;   // static rotation offset (deg)
    float    alpha;        // 0..1 layer opacity
    ColorRef colorRef;     // BODY / WHITE / PALETTE
    Color    paletteCol;   // used when colorRef == COLREF_PALETTE
    ColorRef outlineRef;   // outline colour ref (or none)
    bool     hasOutline;
} ShapeLayer;

typedef struct {
    ShapeLayer layers[MAX_SHAPE_LAYERS];
    int        count;
} Geometry;

// Draw a geometry at p with the given entity radius and base (body) colour. `t` is runTime
// for spin animation.
void geometry_draw(const Geometry *g, Vector2 p, float radius, Color body, float t);

#endif // ANGERBLAST_GEOMETRY_H
