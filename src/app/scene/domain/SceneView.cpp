#include "SceneView.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDraws.h"
#include "app/scene/domain/ActorMarkers.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "hackeroot/GameCode.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize SettingsPerDay = 4;
constexpr usize Midday = 1;

[[nodiscard]] bool isShown(const SceneView& view, const usize room) {
    return room >= view.hiddenRooms.size() || !view.hiddenRooms[room];
}

struct Placed {
    std::vector<PlacedActor> actors;
    std::vector<PlacedActor> transitionActors;
    std::vector<PlacedActor> entrances;
};

[[nodiscard]] PlacedActor makePlaced(const ActorPlacement& actor,
                                     const std::optional<actor::Age> age) {
    return PlacedActor{.id = actor.id,
                       .position = actor.position,
                       .rotation = actor.rotation,
                       .params = static_cast<u16>(actor.params),
                       .age = age};
}

[[nodiscard]] Placed collectPlaced(const SceneLayer& layer, const SceneView& view) {
    const std::optional<actor::Age> age = findLayerAge(layer.index);
    Placed placed;
    for (usize room = 0; room < layer.rooms.size(); ++room) {
        if (!isShown(view, room)) {
            continue;
        }
        for (const ActorPlacement& actor : layer.rooms[room].actors) {
            placed.actors.push_back(makePlaced(actor, age));
        }
    }
    for (const TransitionActorPlacement& actor : layer.header.transitionActors) {
        placed.transitionActors.push_back(PlacedActor{.id = actor.id,
                                                      .position = actor.position,
                                                      .rotation = {0, actor.rotationY, 0},
                                                      .params = static_cast<u16>(actor.params),
                                                      .age = age});
    }
    for (const ActorPlacement& entry : layer.header.playerEntries) {
        placed.entrances.push_back(makePlaced(entry, age));
    }
    return placed;
}

/// Draws `placed` as their models, the entrance shown among them, and leaves
/// in `placed` only what is still to be marked.
[[nodiscard]] ActorDraws drawModels(const Scene& scene, const SceneView& view, Placed& placed,
                                    const usize firstImage, const hackeroot::GameSetup& setup) {
    std::vector<PlacedActor> all = placed.actors;
    all.insert(all.end(), placed.transitionActors.begin(), placed.transitionActors.end());
    const bool showsEntrance = view.shownEntrance < placed.entrances.size();
    if (showsEntrance) {
        all.push_back(placed.entrances[view.shownEntrance]);
    }
    const z64::MemoryImage& code = *scene.images.front();
    const ActorSetup actorSetup{
        .setupDisplayList = setup.setupDisplayList,
        .shadowDisplayList = hackeroot::findSetupDisplayList(code, hackeroot::SetupDisplayList44),
        .emptyDisplayList = code.findAddress("gEmptyDL"),
        .frame = view.actorFrame};
    ActorDraws draws = makeActorDraws(scene.actors, all, firstImage, actorSetup);
    Placed undrawn;
    const usize actorCount = placed.actors.size();
    const usize transitionEnd = actorCount + placed.transitionActors.size();
    bool isEntranceDrawn = showsEntrance;
    for (const usize index : draws.undrawn) {
        if (index < actorCount) {
            undrawn.actors.push_back(all[index]);
        } else if (index < transitionEnd) {
            undrawn.transitionActors.push_back(all[index]);
        } else {
            isEntranceDrawn = false;
        }
    }
    for (usize i = 0; i < placed.entrances.size(); ++i) {
        if (!isEntranceDrawn || i != view.shownEntrance) {
            undrawn.entrances.push_back(placed.entrances[i]);
        }
    }
    placed = std::move(undrawn);
    return draws;
}

[[nodiscard]] std::vector<Marker> makeMarkers(const Placed& placed) {
    std::vector<Marker> markers;
    for (const PlacedActor& actor : placed.actors) {
        markers.push_back(Marker{.position = actor.position, .kind = MarkerKind::Actor});
    }
    for (const PlacedActor& entry : placed.entrances) {
        markers.push_back(Marker{.position = entry.position, .kind = MarkerKind::PlayerEntry});
    }
    for (const PlacedActor& actor : placed.transitionActors) {
        markers.push_back(Marker{.position = actor.position, .kind = MarkerKind::TransitionActor});
    }
    return markers;
}

void addMarkers(viewport::ViewportContent& content, const std::span<const Marker> markers) {
    if (markers.empty()) {
        return;
    }
    ActorMarkers made = makeActorMarkers(markers);
    content.draws.push_back(
        viewport::ViewportDraw{.image = content.images.size(), .displayList = made.displayList});
    content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(made.image)));
}

struct PassSource {
    const SceneLayer& layer;
    const SceneView& view;
    const hackeroot::GameSetup& setup;
    usize imageCount = 0;
};

[[nodiscard]] std::vector<viewport::ViewportDraw> makePass(const PassSource& source,
                                                           const bool opaque) {
    std::vector<viewport::ViewportDraw> draws;
    if (source.setup.drawConfig) {
        draws.push_back(
            viewport::ViewportDraw{.image = 0, .displayList = *source.setup.drawConfig});
    }
    if (source.setup.setupDisplayList) {
        draws.push_back(
            viewport::ViewportDraw{.image = 0, .displayList = *source.setup.setupDisplayList});
    }
    const std::vector<RoomHeader>& rooms = source.layer.rooms;
    for (usize room = 0; room < rooms.size() && FirstRoomImage + room < source.imageCount; ++room) {
        if (!isShown(source.view, room)) {
            continue;
        }
        for (const RoomDisplayLists& lists : rooms[room].displayLists) {
            const u32 displayList = opaque ? lists.opaque : lists.translucent;
            if (displayList != 0) {
                draws.push_back(viewport::ViewportDraw{.image = FirstRoomImage + room,
                                                       .displayList = displayList});
            }
        }
    }
    return draws;
}

[[nodiscard]] usize findDefaultLightSetting(const SceneHeader& header) {
    if (header.lightMode == LightMode::Time && Midday < header.lightSettings.size()) {
        return Midday;
    }
    return 0;
}

} // namespace

SceneView makeSceneView(const Scene& scene) {
    SceneView view;
    if (!scene.layers.empty()) {
        view.lightSetting = findDefaultLightSetting(scene.layers.front().header);
        view.hiddenRooms.assign(scene.layers.front().rooms.size(), false);
    }
    return view;
}

SceneView keepSceneView(const Scene& scene, SceneView view) {
    if (scene.layers.empty()) {
        return makeSceneView(scene);
    }
    view.layer = std::min(view.layer, scene.layers.size() - 1);
    const SceneLayer& layer = scene.layers[view.layer];
    if (view.lightSetting >= layer.header.lightSettings.size()) {
        view.lightSetting = findDefaultLightSetting(layer.header);
    }
    view.hiddenRooms.resize(layer.rooms.size(), false);
    return view;
}

SceneView restoreSceneView(const Scene& scene, const core::SceneSession& session) {
    SceneView view = makeSceneView(scene);
    const auto layer = std::ranges::find(scene.layers, session.layer, &SceneLayer::index);
    if (layer != scene.layers.end()) {
        view.layer = static_cast<usize>(layer - scene.layers.begin());
    }
    view.lightSetting = session.lightSetting;
    view.showsActors = session.marksActors;
    view.drawsModels = session.drawsModels;
    view = keepSceneView(scene, std::move(view));
    for (const usize room : session.hiddenRooms) {
        if (room < view.hiddenRooms.size()) {
            view.hiddenRooms[room] = true;
        }
    }
    return view;
}

core::SceneSession makeSceneSession(const Scene& scene, const SceneView& view) {
    core::SceneSession session{.directory = scene.directory,
                               .lightSetting = view.lightSetting,
                               .marksActors = view.showsActors,
                               .drawsModels = view.drawsModels};
    if (view.layer < scene.layers.size()) {
        session.layer = scene.layers[view.layer].index;
    }
    for (usize room = 0; room < view.hiddenRooms.size(); ++room) {
        if (view.hiddenRooms[room]) {
            session.hiddenRooms.push_back(room);
        }
    }
    return session;
}

viewport::ViewportContent makeSceneContent(const Scene& scene, const SceneView& view) {
    viewport::ViewportContent content{.images = scene.images};
    if (scene.images.empty() || view.layer >= scene.layers.size()) {
        return content;
    }
    const SceneLayer& layer = scene.layers[view.layer];
    const hackeroot::GameSetup setup = hackeroot::findGameSetup(*scene.images.front());
    const PassSource source{
        .layer = layer, .view = view, .setup = setup, .imageCount = scene.images.size()};

    content.draws = makePass(source, true);
    std::vector<viewport::ViewportDraw> translucentActors;
    if (view.showsActors) {
        Placed placed = collectPlaced(layer, view);
        if (view.drawsModels) {
            ActorDraws draws = drawModels(scene, view, placed, content.images.size(), setup);
            std::ranges::move(draws.images, std::back_inserter(content.images));
            std::ranges::move(draws.opaque, std::back_inserter(content.draws));
            translucentActors = std::move(draws.translucent);
        }
        addMarkers(content, makeMarkers(placed));
    }
    std::ranges::move(makePass(source, false), std::back_inserter(content.draws));
    std::ranges::move(translucentActors, std::back_inserter(content.draws));

    if (view.lightSetting < layer.header.lightSettings.size()) {
        content.lighting = makeLighting(layer.header.lightSettings[view.lightSetting]);
    }
    return content;
}

viewport::ViewportLighting makeLighting(const LightSetting& setting) {
    return viewport::ViewportLighting{
        .ambient = setting.ambientColor,
        .lights = {viewport::ViewportLight{.direction = setting.light1Direction,
                                           .color = setting.light1Color},
                   viewport::ViewportLight{.direction = setting.light2Direction,
                                           .color = setting.light2Color}},
        .fogColor = setting.fogColor,
        .fogNear = setting.fogNear,
        .zFar = static_cast<f32>(setting.zFar) > viewport::OrbitCamera::Near
                    ? static_cast<f32>(setting.zFar)
                    : viewport::OrbitCamera::Far,
    };
}

std::string makeLightSettingName(const SceneHeader& header, const usize index) {
    if (header.lightMode != LightMode::Time) {
        return std::format("Setting {}", index);
    }
    constexpr std::array<const char*, SettingsPerDay> Times{"Dawn", "Day", "Dusk", "Night"};
    const usize config = index / SettingsPerDay;
    const char* time = Times[index % SettingsPerDay];
    return config == 0 ? std::string(time) : std::format("{}, config {}", time, config);
}

} // namespace toki::scene
