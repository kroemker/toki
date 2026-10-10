typedef enum ItemID {
    /* 0x00 */ ITEM_DEKU_STICK,
    /* 0x01 */ ITEM_DEKU_NUT,
    /* 0xFF */ ITEM_NONE = 0xFF
} ItemID;

typedef enum GetItemID {
    /* 0x00 */ GI_NONE,
    /* 0x01 */ GI_BOMBS_5,
#if CAN_INCLUDE_TEST_SCENES
    /* 0x02 */ GI_TEST_ONLY,
#endif
    /* 0x02 */ GI_HEART_PIECE,
    /* 0x03 */ GI_MAX
} GetItemID;

typedef enum GetItemDrawID {
    /* 0x00 */ GID_BOTTLE_EMPTY
} GetItemDrawID;
