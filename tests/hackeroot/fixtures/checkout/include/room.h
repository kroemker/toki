#ifndef ROOM_H
#define ROOM_H

#include "ultra64.h"

#define NULL ((void*)0)
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))
#define VTX(x, y, z, s, t, r, g, b, a) { { { x, y, z }, 0, { s, t }, { r, g, b, a } } }

typedef enum RoomShapeType {
    ROOM_SHAPE_TYPE_NORMAL,
    ROOM_SHAPE_TYPE_IMAGE
} RoomShapeType;

typedef struct RoomShapeDListsEntry {
    Gfx* opa;
    Gfx* xlu;
} RoomShapeDListsEntry;

typedef struct RoomShapeNormal {
    u8 type;
    u8 numEntries;
    RoomShapeDListsEntry* entries;
    RoomShapeDListsEntry* entriesEnd;
} RoomShapeNormal;

#endif
