#include "room.h"

ActorEntry sActorList[LENGTH_sActorList] = {
    { ACTOR_PLAYER, { 0, -120, 0 }, { 0, 0, 0 }, 0x0F00 },
#if ENABLE_EXTRA_ACTOR
    { ACTOR_EN_TEST, { 1, 2, 3 }, { 0, 0, 0 }, 0 },
#endif
};

u64 sFloorTex[256] = {
#include "sFloorTex.rgba16.inc.c"
};

u8 sPalette[] = {
#include "sPalette.ci8.inc.c"
};

Vtx sVertices[] = {
    { { { -219, -120, 219 }, 0, { -736, 1480 }, { 0, 127, 0, 255 } } },
    { { { 219, -120, 219 }, 0, { 1728, 1480 }, { 0, 127, 0, 255 } } },
};

Gfx sOpaque[] = {
    gsSPVertex(sVertices, 2, 0),
    gsSP1Triangle(0, 1, 0, 0),
    gsSPEndDisplayList(),
};

s16 sAngle = DEG_TO_BINANG(0.000);
