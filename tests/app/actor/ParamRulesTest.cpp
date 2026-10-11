#include "app/actor/domain/ParamRules.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/OverlayCode.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "z64/DecompDialect.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<toki::actor::ActorParam> readParams(std::string source) {
    const cparser::MacroTable base;
    const std::filesystem::path main =
        std::filesystem::path(TOKI_TEST_FIXTURES) / "app/actor/fixtures/ovl_En_Test/z_en_test.c";
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, main, std::move(source),
                                cparser::PreprocessOptions{.dialect = &z64::getDecompDialect()});
    const toki::actor::OverlayCode code(unit, main);
    return toki::actor::findActorParams(code);
}

} // namespace

TEST_CASE("params are found where the C reads them, named by what they are read into",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
#define TYPE_SHIFT 12
void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    s32 temp = PARAMS_GET_U(thisx->params, 8, 1);
    this->type = PARAMS_GET_U(thisx->params, TYPE_SHIFT, 4);
    if (Flags_GetSwitch(play, PARAMS_GET_U(this->actor.params, 0, 6))) {
    }
    this->speed = PARAMS_GET_S(this->actor.home.rot.z, 0, 4);
    this->overlap = PARAMS_GET_U(thisx->params, 10, 4);
}
void EnTest_Open(EnTest* this, PlayState* play) {
    Actor_OfferGetItemNearby(&this->actor, play, -PARAMS_GET_U(this->actor.params, 6, 2));
    s32 wrong = PARAMS_GET_U(thisx->params, 0, 6);
}
)");

    REQUIRE(params.size() == 4);
    REQUIRE(params[0].name == "Switch Flag");
    REQUIRE(params[0].mask == 0x003F);
    REQUIRE(params[0].kind == toki::actor::ParamKind::Number);
    REQUIRE(params[1].name == "Item");
    REQUIRE(params[1].mask == 0x00C0);
    REQUIRE(params[1].kind == toki::actor::ParamKind::Choice);
    REQUIRE(params[1].collection == "GetItemID");
    REQUIRE(params[2].name == "Bit 8");
    REQUIRE(params[2].kind == toki::actor::ParamKind::Flag);
    REQUIRE(params[3].name == "Type");
    REQUIRE(params[3].mask == 0xF000);
}

TEST_CASE("a param read through a call it is not the whole argument of keeps its own name",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
    Item_DropCollectible(play, &thisx->world.pos, PARAMS_GET_U(thisx->params, 0, 5) | 0x8000);
    Item_DropCollectible(play, &thisx->world.pos, PARAMS_GET_U(thisx->params, 8, 5));
}
)");

    REQUIRE(params.size() == 2);
    REQUIRE(params[0].name == "Bits 0 to 4");
    REQUIRE_FALSE(params[0].collection);
    REQUIRE(params[1].name == "Drop");
    REQUIRE(params[1].collection == "Item00Type");
}

TEST_CASE("a param played as a sound or as music takes its values from the checkout's",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
    Audio_PlayFanfare(PARAMS_GET_U(thisx->params, 8, 8));
    Actor_PlaySfx(thisx, PARAMS_GET_U(thisx->params, 0, 8));
}
)");

    REQUIRE(params.size() == 2);
    REQUIRE(params[0].name == "Sound");
    REQUIRE(params[0].collection == "SfxId");
    REQUIRE(params[1].name == "Music");
    REQUIRE(params[1].collection == "NA_BGM");
}

TEST_CASE("a param that picks an entry of a table of constants chooses from that table",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
static s16 sGetItemIds[] = { GI_RUPEE_GOLD, GI_HEART_PIECE };
static f32 sScales[] = { 1.0f, 2.0f };
void EnTest_Init(Actor* thisx, PlayState* play) {
    static s16 sSounds[] = { 0, NA_SE_EN_DODO_J_WALK - SFX_FLAG, NA_SE_EN_DODO_J_CRY };
    this->getItemId = sGetItemIds[PARAMS_GET_U(thisx->params, 0, 4)];
    Actor_PlaySfx(thisx, sSounds[PARAMS_GET_U(thisx->params, 4, 4) - 1]);
    this->scale = sScales[PARAMS_GET_U(thisx->params, 8, 4)];
}
)");

    REQUIRE(params.size() == 3);
    REQUIRE(params[0].name == "Get Item Id");
    REQUIRE(params[0].kind == toki::actor::ParamKind::Choice);
    REQUIRE(params[0].collection == "sGetItemIds");
    REQUIRE(params[1].name == "Sound");
    REQUIRE(params[1].collection == "sSounds");
    REQUIRE(params[1].offset == -1);
    REQUIRE(params[2].name == "Bits 8 to 11");
    REQUIRE_FALSE(params[2].collection);
}

TEST_CASE("a table lookup is followed through a local and found on the whole params",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> local = readParams(R"(
static s16 sObjectIds[] = { OBJECT_GI_KEY, OBJECT_GI_HEART };
static s16 sGetItemIds[] = { GI_SMALL_KEY, GI_HEART_PIECE };
void EnTest_Init(Actor* thisx, PlayState* play) {
    s32 type = PARAMS_GET_U(thisx->params, 0, 8);
    s32 slot = Object_GetSlot(&play->objectCtx, sObjectIds[type]);
    Actor_OfferGetItem(thisx, play, sGetItemIds[type], 30.0f, 50.0f);
}
)");
    REQUIRE(local.size() == 1);
    REQUIRE(local[0].name == "Type");
    REQUIRE(local[0].collection == "sGetItemIds");

    const std::vector<toki::actor::ActorParam> whole = readParams(R"(
static s16 sGetItemIds[] = { GI_RUPEE_GOLD, GI_HEART_PIECE };
void EnTest_Give(EnTest* this, PlayState* play) {
    u16 getItemId = sGetItemIds[this->actor.params];
    s32 other = sGetItemIds[this->actor.params + 1];
}
)");
    REQUIRE(whole.size() == 1);
    REQUIRE(whole[0].mask == 0xFFFF);
    REQUIRE(whole[0].name == "Get Item Id");
    REQUIRE(whole[0].collection == "sGetItemIds");
}

TEST_CASE("a lookup on params an actor has narrowed to some of their bits is of those bits",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    this->pathIndex = PARAMS_GET_U(this->actor.params, 8, 8);
    this->actor.params = PARAMS_GET_U(this->actor.params, 0, 8);
}
void EnTest_Draw(Actor* thisx, PlayState* play) {
    static s16 soundEffects[] = {
        0,
        NA_SE_EV_WATER_WALL - SFX_FLAG,
    };
    EnTest* this = (EnTest*)thisx;
    if (this->actor.params == 1) {
        Sfx_PlaySfxCentered2(soundEffects[this->actor.params]);
    } else {
        Actor_PlaySfx(&this->actor, soundEffects[this->actor.params]);
    }
}
)");

    REQUIRE(params.size() == 2);
    REQUIRE(params[0].name == "Sound");
    REQUIRE(params[0].mask == 0x00FF);
    REQUIRE(params[0].collection == "soundEffects");
    REQUIRE(params[1].name == "Path Index");
}

TEST_CASE("a read through the overlay's own macro is a param named by the macro",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
#define DOORSHUTTER_GET_SWITCH_FLAG(actor) PARAMS_GET_U((actor)->params, 0, 6)
#define DOORSHUTTER_GET_TYPE(thisx) (PARAMS_GET_U((thisx)->params, 6, 4))
#define MOVEBG_KIND(params) PARAMS_GET_U(params, 12, 4)
void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    this->type = DOORSHUTTER_GET_TYPE(thisx);
    if (Flags_GetSwitch(play, DOORSHUTTER_GET_SWITCH_FLAG(&this->actor))) {
        Flags_SetSwitch(play, DOORSHUTTER_GET_SWITCH_FLAG(&this->actor));
    }
    s32 kind = MOVEBG_KIND(thisx->params);
}
)");

    REQUIRE(params.size() == 3);
    REQUIRE(params[0].name == "Switch Flag");
    REQUIRE(params[0].mask == 0x003F);
    REQUIRE(params[0].sceneFlag == toki::actor::SceneFlag::Switch);
    REQUIRE(params[0].flagUse == toki::actor::FlagUse::Both);
    REQUIRE(params[1].name == "Type");
    REQUIRE(params[1].mask == 0x03C0);
    REQUIRE(params[2].mask == 0xF000);
    REQUIRE(params[2].name == "Kind");
}

TEST_CASE("a member assigned a read once is followed to the flags it is passed to",
          "[actor][params]") {
    const std::vector<toki::actor::ActorParam> params = readParams(R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    this->chestFlag = PARAMS_GET_U(thisx->params, 0, 5);
    this->lit = PARAMS_GET_U(thisx->params, 10, 1);
    s32 bits = PARAMS_GET_U(thisx->params, 6, 4);
    if (Flags_GetCollectible(play, bits)) {
    }
}
void EnTest_Open(EnTest* this, PlayState* play) {
    if (!Flags_GetTreasure(play, this->chestFlag)) {
        Flags_SetTreasure(play, this->chestFlag);
    }
}
)");

    REQUIRE(params.size() == 3);
    REQUIRE(params[0].name == "Chest Flag");
    REQUIRE(params[0].sceneFlag == toki::actor::SceneFlag::Treasure);
    REQUIRE(params[0].flagUse == toki::actor::FlagUse::Both);
    REQUIRE(params[1].name == "Bits");
    REQUIRE(params[1].sceneFlag == toki::actor::SceneFlag::Collectible);
    REQUIRE(params[1].flagUse == toki::actor::FlagUse::Reads);
    REQUIRE(params[2].name == "Lit");
    REQUIRE(params[2].kind == toki::actor::ParamKind::Flag);
}
