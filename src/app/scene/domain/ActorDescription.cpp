#include "ActorDescription.h"

#include "app/actor/domain/ActorModel.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Tables.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::string makeSource(const base::SourceLocation& location) {
    if (location.file.empty()) {
        return {};
    }
    return std::format("  ({}:{})", location.file.filename().string(), location.line);
}

[[nodiscard]] std::string describeSymbol(const char* what, const actor::ObjectSymbol& symbol) {
    return std::format("{} {} in {}{}", what, symbol.name, symbol.object,
                       makeSource(symbol.location));
}

void describeModel(const actor::ActorModel& model, std::vector<std::string>& lines) {
    if (model.skeleton) {
        lines.push_back(
            describeSymbol(model.skeleton->isFlexible ? "Flexible skeleton" : "Skeleton",
                           model.skeleton->skeleton));
        lines.push_back(model.skeleton->animation
                            ? describeSymbol("Animation", *model.skeleton->animation)
                            : std::string("At rest"));
    }
    for (const actor::DisplayListDraw& draw : model.displayLists) {
        lines.push_back(describeSymbol(draw.pass == actor::Pass::Opaque ? "Opaque" : "Translucent",
                                       draw.displayList));
    }
    for (const actor::SegmentSetting& setting : model.segments) {
        lines.push_back(std::format("Segment {} at {}{}", setting.segment, setting.symbol.name,
                                    makeSource(setting.symbol.location)));
    }
    lines.push_back(model.scaleSource
                        ? std::format("Scale {}{}", model.scale, makeSource(*model.scaleSource))
                        : std::format("Scale {}, the default", model.scale));
}

} // namespace

ActorDescription describeActor(const SceneActorModel& actor,
                               const std::optional<std::span<const std::string>> loadedObjects) {
    ActorDescription description{.isDrawn = actor.drawing.has_value()};
    describeModel(actor.model, description.lines);
    for (const actor::ModelGap& gap : actor.model.gaps) {
        description.problems.push_back(gap.diagnostic.message +
                                       makeSource(gap.diagnostic.location));
    }
    for (const base::Diagnostic& diagnostic : actor.diagnostics) {
        description.problems.push_back(diagnostic.message + makeSource(diagnostic.location));
    }
    const std::string& object = actor.model.profileObject;
    if (loadedObjects && !object.empty() && !std::ranges::contains(*loadedObjects, object)) {
        description.problems.push_back(
            std::format("{} is not in the room's object list, so the game does not spawn the "
                        "actor here",
                        object));
    }
    return description;
}

std::vector<std::string> listLoadedObjects(const SceneLayer& layer, const usize room,
                                           const hackeroot::Tables& tables) {
    std::vector<std::string> loaded{hackeroot::KeepObjectName};
    const auto addObject = [&tables, &loaded](const s16 id) {
        const auto found =
            std::ranges::find(tables.objects, static_cast<usize>(id), &hackeroot::ObjectEntry::id);
        if (id > 0 && found != tables.objects.end()) {
            loaded.push_back(found->name);
        }
    };
    addObject(layer.header.keepObjectId);
    if (room < layer.rooms.size()) {
        std::ranges::for_each(layer.rooms[room].objects, addObject);
    }
    return loaded;
}

} // namespace toki::scene
