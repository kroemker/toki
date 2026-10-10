#include "ActorDescription.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ParamReading.h"
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
#include <string_view>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::string makeSource(const base::SourceLocation& location) {
    if (location.file.empty()) {
        return {};
    }
    return std::format("  ({}:{})", location.file.filename().string(), location.line);
}

[[nodiscard]] std::string describeObject(const std::optional<std::string>& own,
                                         const std::optional<std::string>& inherited) {
    const std::optional<std::string>& object = own ? own : inherited;
    return object ? std::format(" in {}", *object) : std::string();
}

[[nodiscard]] std::string describePass(const actor::Pass pass) {
    return pass == actor::Pass::Opaque ? "Opaque" : "Translucent";
}

void describePart(const actor::ActorPart& part, const std::optional<std::string>& object,
                  std::vector<std::string>& lines) {
    if (part.kind == actor::PartKind::Skeleton) {
        lines.push_back(
            std::format("Skeleton {}{}", part.symbol, describeObject(part.object, object)));
        lines.push_back(part.pose ? std::format("Posed as {}{}", *part.pose,
                                                describeObject(part.poseObject, object))
                                  : std::string("At rest"));
        return;
    }
    lines.push_back(std::format("{} {}{}", describePass(part.pass), part.symbol,
                                describeObject(part.object, object)));
}

void describeVariant(const actor::ActorEntry& entry, const actor::ActorVariant& variant,
                     std::vector<std::string>& lines) {
    const std::optional<std::string>& object = variant.object ? variant.object : entry.object;
    const actor::DrawKind kind = variant.draw.value_or(entry.draw.value_or(actor::DrawKind::Model));
    if (kind == actor::DrawKind::Marker) {
        lines.emplace_back("Marked: it has no model by design");
    }
    for (const actor::ActorPart& part : variant.parts) {
        describePart(part, object, lines);
    }
    for (const actor::ChildActor& child : variant.children) {
        lines.push_back(std::format("Spawns {} with params 0x{:04X}", child.actor, child.params));
    }
    lines.push_back(std::format(
        "Scale {}", variant.scale.value_or(entry.scale.value_or(actor::ActorModel::DefaultScale))));
}

} // namespace

ActorDescription describeActor(const SceneActorModel& actor,
                               const actor::PlacementValues& placement,
                               const std::optional<std::span<const std::string>> loadedObjects) {
    const ActorDrawing* drawing = actor.findDrawing(placement);
    const actor::ActorVariant* variant =
        actor.entry ? actor::findVariant(*actor.entry, placement) : nullptr;
    ActorDescription description{
        .isDrawn = drawing != nullptr && drawing->isDrawn(),
        .canAddVariant = actor.entry && !actor.entry->params.empty(),
    };
    if (actor.entry) {
        description.name = actor.entry->name;
        description.params = actor::readParams(*actor.entry, placement);
    } else {
        description.problems.emplace_back(
            "It has no entry in the actor database, so it is marked; derive one in the Actor "
            "Database panel");
    }
    if (variant != nullptr) {
        describeVariant(*actor.entry, *variant, description.lines);
    } else if (actor.entry && !actor.entry->variants.empty()) {
        description.problems.push_back(
            std::format("no variant of its entry matches its params 0x{:04X}", placement.params));
    }
    for (const base::Diagnostic& diagnostic : actor.diagnostics) {
        description.problems.push_back(diagnostic.message + makeSource(diagnostic.location));
    }
    const std::optional<std::string> object = actor.entry ? actor.entry->object : std::nullopt;
    if (loadedObjects && object && !std::ranges::contains(*loadedObjects, *object)) {
        description.problems.push_back(
            std::format("{} is not in the room's object list, so the game does not spawn the "
                        "actor here",
                        *object));
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
