#include "app/actor/domain/DrawSteps.h"

#include "app/actor/domain/ActorEntry.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

TEST_CASE("a skeleton's keys become its colours, segments and the skeleton", "[actor][steps]") {
    const toki::actor::ActorVariant variant{
        .skeleton = "gMidoSkel",
        .animation = "gMidoIdleAnim",
        .frame = 3,
        .segments = {toki::actor::SegmentSymbol{.segment = 8, .name = "gMidoEyeOpenTex"}},
        .colors = {.environment = std::array<base::u8, 4>{0, 0, 0, 255}},
    };

    const std::vector<toki::actor::DrawStep> steps = toki::actor::makeDrawSteps(variant);

    REQUIRE(steps.size() == 3);
    REQUIRE(std::holds_alternative<toki::actor::ColorsStep>(steps[0].step));
    REQUIRE(std::get<toki::actor::SegmentStep>(steps[1].step).symbol == "gMidoEyeOpenTex");
    const auto& skeleton = std::get<toki::actor::SkeletonStep>(steps[2].step);
    REQUIRE(skeleton.skeleton == "gMidoSkel");
    REQUIRE(skeleton.animation == "gMidoIdleAnim");
    REQUIRE(skeleton.frame == 3U);
}

TEST_CASE("each display list's keys start again from the actor's matrix", "[actor][steps]") {
    const toki::actor::ActorVariant variant{
        .displayLists = {toki::actor::DisplayListPart{.name = "gTorchDL"},
                         toki::actor::DisplayListPart{
                             .name = "gEffFire1DL",
                             .object = "gameplay_keep",
                             .pass = toki::actor::Pass::Translucent,
                             .placement = {.translation = {0.0F, 52.0F, 0.0F}},
                             .colors = {.primitive = std::array<base::u8, 4>{255, 255, 0, 255}},
                             .segments = {toki::actor::SegmentSymbol{.segment = 8,
                                                                     .name = "gFireTex"}}}},
        .colors = {.environment = std::array<base::u8, 4>{255, 0, 0, 0}},
        .isBillboard = true,
    };

    const std::vector<toki::actor::DrawStep> steps = toki::actor::makeDrawSteps(variant);

    REQUIRE(steps.size() == 7);
    const auto& first = std::get<toki::actor::TransformStep>(steps[0].step);
    REQUIRE(first.resets);
    REQUIRE(first.isBillboard);
    REQUIRE(std::get<toki::actor::DisplayListStep>(steps[2].step).name == "gTorchDL");
    const auto& flame = std::get<toki::actor::TransformStep>(steps[3].step);
    REQUIRE(flame.placement.translation[1] == 52.0F);
    const auto& colors = std::get<toki::actor::ColorsStep>(steps[4].step).colors;
    REQUIRE(colors.primitive.has_value());
    REQUIRE(colors.environment.has_value());
    REQUIRE(std::get<toki::actor::SegmentStep>(steps[5].step).object == "gameplay_keep");
    REQUIRE(std::get<toki::actor::DisplayListStep>(steps[6].step).pass ==
            toki::actor::Pass::Translucent);
}

TEST_CASE("a variant's own steps are drawn as they are", "[actor][steps]") {
    const toki::actor::ActorVariant variant{
        .skeleton = "gIgnoredSkel",
        .steps = {toki::actor::DrawStep{.step = toki::actor::SetupStep{.index = 44}}},
    };

    const std::vector<toki::actor::DrawStep> steps = toki::actor::makeDrawSteps(variant);

    REQUIRE(steps.size() == 1);
    REQUIRE(std::get<toki::actor::SetupStep>(steps[0].step).index == 44);
}

TEST_CASE("a segment only an earlier display list set points at nothing again", "[actor][steps]") {
    const toki::actor::ActorVariant variant{
        .displayLists = {toki::actor::DisplayListPart{.name = "gSignDL",
                                                      .segments = {toki::actor::SegmentSymbol{
                                                          .segment = 8, .name = "gSignTex"}}},
                         toki::actor::DisplayListPart{.name = "gPostDL"}},
    };

    const std::vector<toki::actor::DrawStep> steps = toki::actor::makeDrawSteps(variant);

    REQUIRE(steps.size() == 6);
    const auto& cleared = std::get<toki::actor::SegmentStep>(steps[3].step);
    REQUIRE(cleared.segment == 8);
    REQUIRE(cleared.symbol == toki::actor::EmptySegment);
}
