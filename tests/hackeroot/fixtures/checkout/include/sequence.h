#define DEFINE_SEQUENCE(_0, seqId, _2, _3, _4) seqId,
#define DEFINE_SEQUENCE_PTR(_0, seqId, _2, _3, _4) seqId,
typedef enum {
#include "tables/sequence_table.h"
    NA_BGM_MAX,

    NA_BGM_NO_MUSIC = 0x7F,
    NA_BGM_DISABLED = 0xFFFF
} NA_BGM;
#undef DEFINE_SEQUENCE
#undef DEFINE_SEQUENCE_PTR
