#include "ultra64.h"

#define SOME_FLAG (1 << 3)

/* a block comment
   spanning several lines */
static const u32 sValues[] = {
    0x0A, 010, 42u, 0xFFFFFFFFuL,
    SOME_FLAG | 0x80, // trailing line comment
    -1,
};

static const f32 sFloats[] = { 1.0f, .5f, 2.5e-3f, 3.0 };

static const char* sText = "a \"quoted\" string";
static const char sChar = '\0';

Gfx* draw(PlayState* play) {
    return play->state.gfxCtx->polyOpa.p;
}
