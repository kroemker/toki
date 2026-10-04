#include "ultra64.h"
#include "room.h"

Vtx custom_room_0Vtx[] = {
    VTX(1, 2, 3, 4, 5, 6, 7, 8, 9),
};

Gfx custom_room_0DL[] = {
    gsSPVertex(custom_room_0Vtx, 1, 0),
    gsSPEndDisplayList(),
};

RoomShapeDListsEntry custom_room_0Entries[] = {
    { custom_room_0DL, NULL },
};

RoomShapeNormal custom_room_0Shape = {
    ROOM_SHAPE_TYPE_NORMAL,
    ARRAY_COUNT(custom_room_0Entries),
    custom_room_0Entries,
    custom_room_0Entries + ARRAY_COUNT(custom_room_0Entries),
};
