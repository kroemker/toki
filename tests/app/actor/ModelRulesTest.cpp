#include "app/actor/domain/ModelRules.h"

#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/OverlayCode.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "z64/DecompDialect.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "app/actor/fixtures";
}

[[nodiscard]] toki::actor::ActorModel readModel(std::string source,
                                                const bool namesMainOtherwise = false) {
    const cparser::MacroTable base;
    const std::filesystem::path written =
        (getFixtures() / "ovl_En_Test" / "z_en_test.c").generic_string();
    const std::filesystem::path main =
        namesMainOtherwise ? std::filesystem::path(written).make_preferred() : written;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, written, "#include \"assets/objects/object_test/object_test.h\"\n" + source,
        cparser::PreprocessOptions{.searchPaths = {getFixtures()},
                                   .allowedRoots = {getFixtures()},
                                   .dialect = &z64::getDecompDialect()});
    const toki::actor::OverlayCode code(unit, main);
    return toki::actor::applyModelRules(
        "En_Test", code, [](const std::string_view enumName) -> std::optional<std::string> {
            if (enumName == "OBJECT_TEST") {
                return "object_test";
            }
            return std::nullopt;
        });
}

[[nodiscard]] bool hasGap(const toki::actor::ActorModel& model, const toki::actor::GapKind kind) {
    return std::ranges::any_of(
        model.gaps, [kind](const toki::actor::ModelGap& gap) { return gap.kind == kind; });
}

constexpr std::string_view Profile = R"(
ActorProfile En_Test_Profile = {
    /**/ ACTOR_EN_TEST,
    /**/ ACTORCAT_NPC,
    /**/ FLAGS,
    /**/ OBJECT_TEST,
    /**/ sizeof(EnTest),
    /**/ EnTest_Init,
    /**/ EnTest_Destroy,
    /**/ EnTest_Update,
    /**/ EnTest_Draw,
};
)";

} // namespace

TEST_CASE("a skeleton, its animation, the init chain's scale and the height offset are read",
          "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(std::string(Profile) + R"(
static InitChainEntry sInitChain[] = {
    ICHAIN_F32(lockOnArrowOffset, 500, ICHAIN_CONTINUE),
    ICHAIN_VEC3F_DIV1000(scale, 15, ICHAIN_STOP),
};

void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    Actor_ProcessInitChain(&this->actor, sInitChain);
    ActorShape_Init(&this->actor.shape, 10.0f, ActorShadow_DrawCircle, 30.0f);
    SkelAnime_InitFlex(play, &this->skelAnime, &gTestSkel, &gTestWalkAnim, this->jointTable,
                       this->morphTable, 12);
}

void EnTest_Draw(Actor* thisx, PlayState* play) {
    SkelAnime_DrawFlexOpa(play, this->skelAnime.skeleton, this->skelAnime.jointTable,
                          this->skelAnime.dListCount, NULL, NULL, this);
}
)");

    REQUIRE(model.profileObject == "object_test");
    REQUIRE(model.skeleton.has_value());
    REQUIRE(model.skeleton->skeleton.name == "gTestSkel");
    REQUIRE(model.skeleton->skeleton.object == "object_test");
    REQUIRE(model.skeleton->isFlexible);
    REQUIRE(model.skeleton->animation.has_value());
    REQUIRE(model.skeleton->animation->name == "gTestWalkAnim");
    REQUIRE(model.scale == Catch::Approx(0.015F));
    REQUIRE(model.scaleSource.has_value());
    REQUIRE(model.yOffset == Catch::Approx(10.0F));
    REQUIRE(model.gaps.empty());
}

TEST_CASE("a skeleton without an animation takes the first one a helper of Init plays",
          "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(std::string(Profile) + R"(
static void EnTest_SetupIdle(EnTest* this) {
    Animation_PlayLoop(&this->skelAnime, &gTestIdleAnim);
}

void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    Actor_SetScale(&this->actor, 0.02f);
    SkelAnime_Init(play, &this->skelAnime, (SkeletonHeader*)&gTestStandardSkel, NULL, NULL, NULL, 0);
    EnTest_SetupIdle(this);
}

void EnTest_Draw(Actor* thisx, PlayState* play) {
}
)");

    REQUIRE(model.skeleton.has_value());
    REQUIRE_FALSE(model.skeleton->isFlexible);
    REQUIRE(model.skeleton->animation.has_value());
    REQUIRE(model.skeleton->animation->name == "gTestIdleAnim");
    REQUIRE(model.scale == Catch::Approx(0.02F));
}

TEST_CASE("an actor without a skeleton draws the display lists its Draw names", "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(std::string(Profile) + R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
}

static void EnTest_DrawGlass(EnTest* this, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx, "z_en_test.c", 40);
    gSPDisplayList(POLY_XLU_DISP++, gTestGlassDL);
    gSPDisplayList(POLY_OPA_DISP++, this->dList);
    CLOSE_DISPS(play->state.gfxCtx, "z_en_test.c", 43);
}

void EnTest_Draw(Actor* thisx, PlayState* play) {
    if (thisx->params == 0) {
        Gfx_DrawDListOpa(play, gTestDL);
    } else {
        Gfx_DrawDListOpa(play, gTestDL);
    }
    EnTest_DrawGlass((EnTest*)thisx, play);
}
)");

    REQUIRE_FALSE(model.skeleton.has_value());
    REQUIRE(model.displayLists.size() == 2);
    REQUIRE(model.displayLists[0].displayList.name == "gTestDL");
    REQUIRE(model.displayLists[0].pass == toki::actor::Pass::Opaque);
    REQUIRE(model.displayLists[1].displayList.name == "gTestGlassDL");
    REQUIRE(model.displayLists[1].pass == toki::actor::Pass::Translucent);
    REQUIRE(model.scale == Catch::Approx(toki::actor::ActorModel::DefaultScale));
    REQUIRE(hasGap(model, toki::actor::GapKind::DisplayListNotLiteral));
}

TEST_CASE("what is not a literal, or not in an object, is a gap", "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(R"(
ActorProfile En_Test_Profile = {
    ACTOR_EN_TEST, ACTORCAT_NPC, FLAGS, OBJECT_TEST, sizeof(EnTest),
    EnTest_Init, EnTest_Destroy, EnTest_Update, NULL,
};

static Gfx sOwnDL[] = { gsSPEndDisplayList() };

void EnTest_Draw(Actor* thisx, PlayState* play) {
    Gfx* dList = sOwnDL;
    Gfx_DrawDListOpa(play, sOwnDL);
    Gfx_DrawDListXlu(play, dList);
}

void EnTest_Init(Actor* thisx, PlayState* play) {
    SkelAnime_InitFlex(play, &this->skelAnime, sSkeletons[thisx->params], NULL, NULL, NULL, 0);
    Actor_SetScale(&this->actor, this->scale);
    this->actor.draw = EnTest_Draw;
}
)");

    REQUIRE_FALSE(model.hasModel());
    REQUIRE(hasGap(model, toki::actor::GapKind::SkeletonNotLiteral));
    REQUIRE(hasGap(model, toki::actor::GapKind::OverlayData));
    REQUIRE(hasGap(model, toki::actor::GapKind::DisplayListNotLiteral));
    REQUIRE(hasGap(model, toki::actor::GapKind::ScaleNotLiteral));
    REQUIRE_FALSE(hasGap(model, toki::actor::GapKind::NoDraw));
}

TEST_CASE("an overlay without a profile has no model", "[actor][rules]") {
    const toki::actor::ActorModel model = readModel("void EnTest_Init(void) {}\n");

    REQUIRE_FALSE(model.hasModel());
    REQUIRE(hasGap(model, toki::actor::GapKind::NoProfile));
}

TEST_CASE("an overlay's functions are found whichever separators its path is written with",
          "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(std::string(Profile) + R"(
void EnTest_Init(Actor* thisx, PlayState* play) {
    SkelAnime_InitFlex(play, &this->skelAnime, &gTestSkel, &gTestWalkAnim, NULL, NULL, 0);
}

void EnTest_Draw(Actor* thisx, PlayState* play) {
}
)",
                                                    true);

    REQUIRE(model.skeleton.has_value());
}

TEST_CASE("display lists picked out of a table by the params are read entry by entry",
          "[actor][rules]") {
    const toki::actor::ActorModel model = readModel(std::string(Profile) + R"(
static Gfx* sDLists[] = { gTestDL, NULL, gTestGlassDL };

void EnTest_Draw(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
    Gfx_DrawDListXlu(play, sDLists[PARAMS_GET_U(thisx->params, 4, 2) - 1]);
    Gfx_DrawDListOpa(play, sDLists[PARAMS_GET_U(thisx->params, 4, 2) - 1]);
    Gfx_DrawDListOpa(play, sDLists[this->type]);
}
)");

    REQUIRE(model.displayLists.empty());
    REQUIRE(model.displayListTables.size() == 1);
    const toki::actor::DisplayListTable& table = model.displayListTables.front();
    REQUIRE(table.table == "sDLists");
    REQUIRE(table.mask == 0x0030);
    REQUIRE(table.offset == -1);
    REQUIRE(table.pass == toki::actor::Pass::Translucent);
    REQUIRE(table.entries.size() == 3);
    REQUIRE(table.entries[0]->name == "gTestDL");
    REQUIRE(table.entries[0]->object == "object_test");
    REQUIRE_FALSE(table.entries[1].has_value());
    REQUIRE(table.entries[2]->name == "gTestGlassDL");
    REQUIRE(hasGap(model, toki::actor::GapKind::DisplayListNotLiteral));
    REQUIRE(model.hasModel());
}
