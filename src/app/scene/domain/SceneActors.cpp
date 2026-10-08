#include "SceneActors.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ActorModelReader.h"
#include "app/actor/domain/GuessedEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDrawingReader.h"
#include "app/scene/domain/ActorObjects.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize ChildLayers = 2;
constexpr usize PlayedLayers = 4;

[[nodiscard]] std::set<s16> collectActorIds(const std::vector<SceneLayer>& layers) {
    std::set<s16> ids;
    for (const SceneLayer& layer : layers) {
        for (const RoomHeader& room : layer.rooms) {
            for (const ActorPlacement& actor : room.actors) {
                ids.insert(actor.id);
            }
        }
        for (const TransitionActorPlacement& actor : layer.header.transitionActors) {
            ids.insert(actor.id);
        }
        for (const ActorPlacement& entry : layer.header.playerEntries) {
            ids.insert(entry.id);
        }
    }
    return ids;
}

[[nodiscard]] std::string formatModified(const std::filesystem::path& file) {
    std::error_code failure;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(file, failure);
    if (failure) {
        return {};
    }
    return std::format("{:%FT%TZ}",
                       std::chrono::floor<std::chrono::seconds>(
                           std::chrono::clock_cast<std::chrono::system_clock>(modified)));
}

class ActorReader {
public:
    ActorReader(hackeroot::AssetImageReader& reader, const std::vector<SceneLayer>& layers,
                const ActorDatabases& databases, std::optional<std::string> scene)
        : workspace(reader.getWorkspace()), layers(layers), databases(databases),
          scene(std::move(scene)), objects(reader, findSceneKeep(), actors) {}

    [[nodiscard]] SceneActors read() &&;

private:
    [[nodiscard]] std::string findSceneKeep() const;

    [[nodiscard]] std::optional<s16> findActorId(std::string_view enumName) const;

    void readActor(s16 id, std::deque<s16>& pending);

    void guessEntry(const hackeroot::ActorEntry& actor, SceneActorModel& model);

    const hackeroot::Workspace& workspace;
    const std::vector<SceneLayer>& layers;
    const ActorDatabases& databases;
    std::optional<std::string> scene;
    SceneActors actors;
    ActorObjects objects;
};

std::string ActorReader::findSceneKeep() const {
    const s16 id = layers.empty() ? s16{0} : layers.front().header.keepObjectId;
    const std::vector<hackeroot::ObjectEntry>& table = workspace.getTables().objects;
    const auto found =
        std::ranges::find(table, static_cast<usize>(id), &hackeroot::ObjectEntry::id);
    return id <= 0 || found == table.end() ? std::string() : found->name;
}

std::optional<s16> ActorReader::findActorId(const std::string_view enumName) const {
    const std::vector<hackeroot::ActorEntry>& table = workspace.getTables().actors;
    const auto found = std::ranges::find(table, enumName, &hackeroot::ActorEntry::enumName);
    return found == table.end() ? std::nullopt : std::optional(static_cast<s16>(found->id));
}

void ActorReader::guessEntry(const hackeroot::ActorEntry& actor, SceneActorModel& model) {
    const auto started = std::chrono::steady_clock::now();
    model.guess = actor::readActorModel(workspace, actor);
    std::optional<actor::GuessSource> source;
    if (const auto overlay = hackeroot::findOverlayFiles(workspace.getRoot(), actor.name)) {
        std::ranges::copy(overlay->getAllFiles(), std::back_inserter(actors.watched));
        source = actor::GuessSource{
            .file = overlay->main.lexically_relative(workspace.getRoot()).generic_string(),
            .modified = formatModified(overlay->main)};
        model.source = ModelSource::Guessed;
    }
    model.entry = actor::makeGuessedEntry(*model.guess, actor.enumName, std::move(source));
    actors.overlaysElapsed += std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
}

void ActorReader::readActor(const s16 id, std::deque<s16>& pending) {
    const std::vector<hackeroot::ActorEntry>& table = workspace.getTables().actors;
    const auto actor = std::ranges::find(table, static_cast<usize>(static_cast<u16>(id)),
                                         &hackeroot::ActorEntry::id);
    if (actor == table.end() || actor->isUnset) {
        return;
    }
    SceneActorModel& model = actors.models[id];
    model.enumName = actor->enumName;
    if (const std::optional<actor::FoundEntry> found =
            actor::findActorEntry(databases.shipped, databases.checkout, actor->enumName)) {
        model.entry = *found->entry;
        model.source = found->origin == actor::EntryOrigin::Checkout ? ModelSource::Checkout
                                                                     : ModelSource::Shipped;
    } else {
        guessEntry(*actor, model);
    }
    const FindActorId findId = [this](const std::string_view name) { return findActorId(name); };
    for (const actor::ActorVariant& variant : model.entry->variants) {
        if (!actor::isMatchingScene(variant, scene)) {
            model.variants.emplace_back();
            continue;
        }
        ActorDrawing drawing =
            makeActorDrawing(*model.entry, variant, objects, findId, model.diagnostics);
        for (const ChildPlacement& child : drawing.children) {
            pending.push_back(child.id);
        }
        model.variants.push_back(std::move(drawing));
    }
}

SceneActors ActorReader::read() && {
    std::vector<base::Diagnostic> keepDiagnostics;
    static_cast<void>(objects.findObject(hackeroot::KeepObjectName, keepDiagnostics));
    if (const std::string sceneKeep = findSceneKeep(); !sceneKeep.empty()) {
        static_cast<void>(objects.findObject(sceneKeep, keepDiagnostics));
    }
    const std::set<s16> placed = collectActorIds(layers);
    std::deque<s16> pending(placed.begin(), placed.end());
    std::set<s16> seen;
    while (!pending.empty()) {
        const s16 id = pending.front();
        pending.pop_front();
        if (seen.insert(id).second) {
            readActor(id, pending);
        }
    }
    return std::move(actors);
}

} // namespace

bool SceneActorModel::hasDrawing() const {
    return std::ranges::any_of(variants, &ActorDrawing::isDrawn);
}

const ActorDrawing* SceneActorModel::findDrawing(const actor::PlacementValues& placement) const {
    if (!entry) {
        return nullptr;
    }
    const actor::ActorVariant* variant = actor::findVariant(*entry, placement);
    if (variant == nullptr) {
        return nullptr;
    }
    const auto index = static_cast<usize>(variant - entry->variants.data());
    return index < variants.size() ? &variants[index] : nullptr;
}

bool hasAnimatedActors(const SceneActors& actors) {
    return std::ranges::any_of(actors.models, [](const auto& entry) {
        return std::ranges::any_of(entry.second.variants, [](const ActorDrawing& drawing) {
            return drawing.skeleton && drawing.skeleton->animation;
        });
    });
}

std::optional<actor::Age> findLayerAge(const usize layerIndex) {
    if (layerIndex >= PlayedLayers) {
        return std::nullopt;
    }
    return layerIndex < ChildLayers ? actor::Age::Child : actor::Age::Adult;
}

SceneActors readSceneActors(hackeroot::AssetImageReader& reader, const std::string_view sceneFolder,
                            const std::vector<SceneLayer>& layers,
                            const ActorDatabases& databases) {
    const auto started = std::chrono::steady_clock::now();
    const std::vector<hackeroot::SceneEntry>& scenes = reader.getWorkspace().getTables().scenes;
    const std::string tableName = std::format("{}_scene", sceneFolder);
    const auto entry = std::ranges::find(scenes, tableName, &hackeroot::SceneEntry::name);
    const std::optional<std::string> scene =
        entry != scenes.end() ? std::optional(entry->enumName) : std::nullopt;
    SceneActors actors = ActorReader(reader, layers, databases, scene).read();
    actors.scene = scene;
    actors.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return actors;
}

actor::PlacementValues makePlacementValues(const SceneActors& actors, const u16 params,
                                           const std::array<s16, 3>& rotation,
                                           const std::optional<actor::Age> age) {
    return actor::PlacementValues{
        .params = params, .rotation = rotation, .age = age, .scene = actors.scene};
}

} // namespace toki::scene
