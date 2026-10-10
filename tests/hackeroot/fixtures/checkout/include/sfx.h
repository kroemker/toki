#define DEFINE_SFX(_0, enum, _2, _3, _4, _5) enum,

typedef enum SfxId {
    NA_SE_NONE,

    NA_SE_EN_BASE = 0x37FF,
    #include "tables/sfx/enemybank_table.h"
    NA_SE_EN_END,

    NA_SE_MAX
} SfxId;
