#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ActorModelReader.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

using base::usize;

namespace {

[[nodiscard]] const toki::actor::ActorModel&
findModel(const std::vector<toki::actor::ActorModel>& models, const std::string& name) {
    const auto found = std::ranges::find(models, name, &toki::actor::ActorModel::actorName);
    REQUIRE(found != models.end());
    return *found;
}

[[nodiscard]] bool hasGap(const toki::actor::ActorModel& model, const toki::actor::GapKind kind) {
    return std::ranges::any_of(
        model.gaps, [kind](const toki::actor::ModelGap& gap) { return gap.kind == kind; });
}

} // namespace

TEST_CASE("every actor of a real checkout is read for how it is drawn", "[actor][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const auto started = std::chrono::steady_clock::now();
    std::vector<toki::actor::ActorModel> models;
    for (const hackeroot::ActorEntry& actor : workspace->getTables().actors) {
        if (!actor.isUnset) {
            models.push_back(toki::actor::readActorModel(*workspace, actor));
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    usize skeletons = 0;
    usize displayLists = 0;
    std::map<std::string, usize> gaps;
    std::string unreadProfiles;
    for (const toki::actor::ActorModel& model : models) {
        skeletons += model.skeleton.has_value() ? 1 : 0;
        displayLists += !model.skeleton && !model.displayLists.empty() ? 1 : 0;
        for (const toki::actor::ModelGap& gap : model.gaps) {
            ++gaps[toki::actor::getGapName(gap.kind)];
            if (gap.kind == toki::actor::GapKind::NoProfile) {
                unreadProfiles += model.actorName + " ";
            }
        }
    }
    std::string described;
    for (const auto& [name, count] : gaps) {
        described += std::to_string(count) + "x " + name + ", ";
    }
    WARN(models.size() << " actors in " << elapsed.count() << " ms: " << skeletons << " skeletons, "
                       << displayLists << " display lists only, "
                       << models.size() - skeletons - displayLists
                       << " without a model; gaps: " << described);

    INFO("profiles not read: " << unreadProfiles);
    CHECK(unreadProfiles.empty());

    const toki::actor::ActorModel& roofMan = findModel(models, "En_Ani");
    REQUIRE(roofMan.skeleton.has_value());
    CHECK(roofMan.profileObject == "object_ani");
    CHECK(roofMan.skeleton->skeleton.name == "gRoofManSkel");
    CHECK(roofMan.skeleton->skeleton.object == "object_ani");
    CHECK(roofMan.skeleton->isFlexible);
    REQUIRE(roofMan.skeleton->animation.has_value());
    CHECK(roofMan.skeleton->animation->name == "gRoofManIdleAnim");

    const toki::actor::ActorModel& dekuBaba = findModel(models, "En_Karebaba");
    REQUIRE(dekuBaba.skeleton.has_value());
    CHECK(dekuBaba.skeleton->skeleton.name == "gDekuBabaSkel");
    CHECK_FALSE(dekuBaba.skeleton->isFlexible);
    REQUIRE(dekuBaba.skeleton->animation.has_value());
    CHECK(dekuBaba.skeleton->animation->name == "gDekuBabaFastChompAnim");

    const toki::actor::ActorModel& sign = findModel(models, "En_Kanban");
    CHECK_FALSE(sign.skeleton.has_value());
    CHECK(std::ranges::any_of(sign.displayLists, [](const toki::actor::DisplayListDraw& draw) {
        return draw.displayList.name == "object_kanban_DL_000C30" &&
               draw.pass == toki::actor::Pass::Opaque;
    }));

    CHECK(hasGap(findModel(models, "Obj_Tsubo"), toki::actor::GapKind::DisplayListNotLiteral));
    CHECK(hasGap(findModel(models, "En_Ko"), toki::actor::GapKind::SkeletonNotLiteral));
    CHECK(hasGap(findModel(models, "Player"), toki::actor::GapKind::NoOverlay));
}
