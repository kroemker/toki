#ifndef ROOM_H
#define ROOM_H

#include "ultra64.h"

#define LENGTH_sActorList 2
#define ENABLE_EXTRA_ACTOR 0

#define TRUNCF_BINANG(f) (s16)(s32)(f)
#define DEG_TO_BINANG(degrees) (s16)TRUNCF_BINANG((degrees) * (0x8000 / 180.0f))

extern ActorEntry sActorList[LENGTH_sActorList];
extern u64 sFloorTex[256];
extern u8 sPalette[];
extern Vtx sVertices[];

#endif
