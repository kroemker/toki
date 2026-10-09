#include "TomlWriting.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

void insertPass(toml::table& table, const Pass pass) {
    if (pass != Pass::Opaque) {
        table.insert("pass", std::string(getEntryName(PassNames, pass)));
    }
}

[[nodiscard]] toml::table makeStep(const SetupStep& step) {
    toml::table table = makeInline();
    table.insert("setup", s64{step.index});
    return table;
}

[[nodiscard]] toml::table makeStep(const ColorsStep& step) {
    toml::table table = makeInline();
    table.insert("colors", makeColors(step.colors));
    return table;
}

[[nodiscard]] toml::table makeStep(const HiliteStep&) {
    toml::table table = makeInline();
    table.insert("hilite", true);
    return table;
}

[[nodiscard]] toml::table makeStep(const SegmentStep& step) {
    toml::table table = makeInline();
    table.insert("segment", s64{step.segment});
    table.insert("symbol", step.symbol);
    insertOptional(table, "object", step.object);
    return table;
}

[[nodiscard]] toml::table makeStep(const TransformStep& step) {
    toml::table transform = makeInline();
    insertPlacement(transform, step.placement);
    if (step.isBillboard) {
        transform.insert("billboard", true);
    }
    if (step.resets) {
        transform.insert("reset", true);
    }
    toml::table table = makeInline();
    table.insert("transform", std::move(transform));
    return table;
}

[[nodiscard]] toml::table makeStep(const DisplayListStep& step) {
    toml::table table = makeInline();
    table.insert("display_list", step.name);
    insertOptional(table, "object", step.object);
    insertPass(table, step.pass);
    return table;
}

[[nodiscard]] toml::table makeStep(const SkeletonStep& step) {
    toml::table table = makeInline();
    table.insert("skeleton", step.skeleton);
    insertOptional(table, "object", step.object);
    insertOptional(table, "animation", step.animation);
    insertOptional(table, "animation_object", step.animationObject);
    if (step.frame) {
        table.insert("frame", s64{*step.frame});
    }
    insertPass(table, step.pass);
    if (!step.limbs.empty()) {
        toml::table limbs = makeInline();
        for (const LimbSteps& limb : step.limbs) {
            limbs.insert(std::to_string(limb.limb), makeSteps(limb.steps));
        }
        table.insert("limbs", std::move(limbs));
    }
    return table;
}

[[nodiscard]] toml::table makeStep(const ShadowStep& step) {
    toml::table table = makeInline();
    table.insert("shadow", makeShadow(step.shadow));
    return table;
}

} // namespace

toml::array makeSteps(const std::vector<DrawStep>& steps) {
    toml::array array;
    for (const DrawStep& step : steps) {
        array.push_back(std::visit([](const auto& kind) { return makeStep(kind); }, step.step));
    }
    return array;
}

} // namespace toki::actor
