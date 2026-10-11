#include "z_en_box.h"

static s16 sGetItemIds[] = { GI_BOMBS_5, GI_HEART_PIECE };

static f32 sScales[] = { 1.0f, 2.0f };

static s16 sSounds[] = { 0, NA_SE_EN_DODO_J_WALK - SFX_FLAG };

void EnBox_Init(Actor* thisx, PlayState* play) {
    static u8 sTypes[] = { ENBOX_TYPE_SMALL, ENBOX_TYPE_BIG };
    static ActorFunc sActions[] = { EnBox_Init, EnBox_Init };
}
