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
