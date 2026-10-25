#include "SceneView.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorDraws.h"
#include "app/scene/domain/ActorMarkers.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionDrawing.h"
#include "app/scene/domain/RuntimeSegments.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "hackeroot/GameCode.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize SettingsPerDay = 4;
constexpr usize Midday = 1;
constexpr f32 PathPointScale = 0.5F;

[[nodiscard]] bool isShown(const SceneView& view, const usize room) {
    return room >= view.hiddenRooms.size() || !view.hiddenRooms[room];
}

[[nodiscard]] bool areRoomsHidden(const SceneView& view) {
    return !view.hiddenRooms.empty() && std::ranges::all_of(view.hiddenRooms, std::identity{});
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
        const std::vector<ActorPlacement>& actors = layer.rooms[room].actors;
        for (usize i = 0; i < actors.size(); ++i) {
            PlacedActor& made = placed.actors.emplace_back(makePlaced(actors[i], age));
            made.owner = makeNodeKey(PlacementRef{
                .kind = PlacementKind::Actor, .layer = view.layer, .room = room, .index = i});
        }
    }
    const std::vector<TransitionActorPlacement>& transitions = layer.header.transitionActors;
    for (usize i = 0; i < transitions.size(); ++i) {
        const TransitionActorPlacement& actor = transitions[i];
        placed.transitionActors.push_back(PlacedActor{
            .id = actor.id,
            .position = actor.position,
            .rotation = {0, actor.rotationY, 0},
            .params = static_cast<u16>(actor.params),
            .age = age,
            .owner = makeNodeKey(PlacementRef{
                .kind = PlacementKind::TransitionActor, .layer = view.layer, .index = i})});
    }
    const std::vector<ActorPlacement>& entries = layer.header.playerEntries;
    for (usize i = 0; i < entries.size(); ++i) {
        PlacedActor& made = placed.entrances.emplace_back(makePlaced(entries[i], age));
        made.owner = makeNodeKey(
            PlacementRef{.kind = PlacementKind::PlayerEntry, .layer = view.layer, .index = i});
    }
    return placed;
}

/// Draws `placed` as their models, the entrance shown among them, and leaves
/// in `placed` only what is still to be marked.
[[nodiscard]] ActorDraws drawModels(const Scene& scene, const SceneView& view, Placed& placed,
                                    const usize firstImage) {
    std::vector<PlacedActor> all = placed.actors;
    all.insert(all.end(), placed.transitionActors.begin(), placed.transitionActors.end());
    const bool showsEntrance = view.shownEntrance < placed.entrances.size();
    if (showsEntrance) {
        all.push_back(placed.entrances[view.shownEntrance]);
    }
    ActorDraws draws =
        makeActorDraws(scene.actors, all, firstImage, makeActorSetup(*scene.images.front()));
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
        markers.push_back(
            Marker{.position = actor.position, .kind = MarkerKind::Actor, .owner = actor.owner});
    }
    for (const PlacedActor& entry : placed.entrances) {
        markers.push_back(Marker{
            .position = entry.position, .kind = MarkerKind::PlayerEntry, .owner = entry.owner});
    }
    for (const PlacedActor& actor : placed.transitionActors) {
        markers.push_back(Marker{
            .position = actor.position, .kind = MarkerKind::TransitionActor, .owner = actor.owner});
    }
    return markers;
}

[[nodiscard]] std::vector<Marker> makePathMarkers(const SceneLayer& layer, const usize shownLayer) {
    std::vector<Marker> markers;
    const std::vector<ScenePath>& paths = layer.header.paths;
    for (usize path = 0; path < paths.size(); ++path) {
        for (usize point = 0; point < paths[path].points.size(); ++point) {
            markers.push_back(Marker{.position = paths[path].points[point],
                                     .kind = MarkerKind::PathPoint,
                                     .scale = PathPointScale,
                                     .owner = makeNodeKey(PathPointRef{
                                         .layer = shownLayer, .path = path, .point = point})});
        }
    }
    return markers;
}

void addMarkers(viewport::ViewportContent& content, const std::span<const Marker> markers,
                const core::MarkerShape shape) {
    if (markers.empty()) {
        return;
    }
    ActorMarkers made = makeActorMarkers(markers, shape);
    for (usize i = 0; i < made.displayLists.size(); ++i) {
        content.draws.push_back(viewport::ViewportDraw{.image = content.images.size(),
                                                       .displayList = made.displayLists[i],
                                                       .owner = markers[i].owner});
    }
    content.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(made.image)));
}

/// Names every image of `content` not yet named `name`.
void labelImages(viewport::ViewportContent& content, const std::string& name,
                 const viewport::ImageKind kind) {
    while (content.labels.size() < content.images.size()) {
        content.labels.push_back(viewport::ImageLabel{.name = name, .kind = kind});
    }
}

void labelSceneImages(viewport::ViewportContent& content, const Scene& scene) {
    for (usize i = 0; i < content.images.size(); ++i) {
        if (i < SceneImage) {
            content.labels.push_back({.name = "Game code", .kind = viewport::ImageKind::Code});
        } else if (i == SceneImage) {
            content.labels.push_back({.name = scene.name, .kind = viewport::ImageKind::Scene});
        } else {
            content.labels.push_back({.name = std::format("Room {}", i - FirstRoomImage),
                                      .kind = viewport::ImageKind::Room});
        }
    }
}

/// Names the images `makeActorDraws` added: each object, then the objects
/// each points into, then the actors' own display lists.
void labelActorImages(viewport::ViewportContent& content, const SceneActors& actors) {
    for (const ActorObject& object : actors.objects) {
        content.labels.push_back({.name = object.name, .kind = viewport::ImageKind::Object});
    }
    for (const ActorObject& object : actors.objects) {
        for (usize i = 0; i < object.linked.size(); ++i) {
            content.labels.push_back({.name = std::format("{}, linked {}", object.name, i + 1),
                                      .kind = viewport::ImageKind::Object});
        }
    }
    labelImages(content, "Placed actors", viewport::ImageKind::Actor);
}

struct PassSource {
    const SceneLayer& layer;
    const SceneView& view;
    const hackeroot::GameSetup& setup;
    usize imageCount = 0;
    usize standInImage = 0;
    u32 standIn = 0;
    /// Whether a click on a room picks it, which it does not while collision
    /// drawn over it is what is picked.
    bool ownsRooms = true;
};

[[nodiscard]] viewport::ViewportDraw makeRuntimeSegmentsDraw(const PassSource& source) {
    viewport::ViewportDraw draw{.image = source.standInImage, .displayList = source.standIn};
    for (u8 segment = FirstRuntimeSegment; segment <= LastRuntimeSegment; ++segment) {
        draw.pointedSegments.push_back(viewport::SegmentPointer{
            .segment = segment, .image = source.standInImage, .address = source.standIn});
    }
    return draw;
}

[[nodiscard]] std::vector<viewport::ViewportDraw> makePass(const PassSource& source,
                                                           const bool opaque) {
    std::vector<viewport::ViewportDraw> draws;
    if (source.setup.drawConfig) {
        draws.push_back(
            viewport::ViewportDraw{.image = 0, .displayList = *source.setup.drawConfig});
    }
    draws.push_back(makeRuntimeSegmentsDraw(source));
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
                viewport::ViewportDraw& draw = draws.emplace_back(viewport::ViewportDraw{
                    .image = FirstRoomImage + room, .displayList = displayList});
                if (source.ownsRooms) {
                    draw.owner = makeNodeKey(RoomRef{.layer = source.view.layer, .room = room});
                }
            }
        }
    }
    return draws;
}

/// The collision's drawing, its image added to `content`.
struct AddedCollision {
    CollisionDrawing drawing;
    usize image = 0;
};

[[nodiscard]] AddedCollision addCollisionImage(viewport::ViewportContent& content,
                                               const Collision& collision, const SceneView& view) {
    AddedCollision added{.drawing = makeCollisionDrawing(
                             collision, CollisionLook{.coloring = view.collisionColoring,
                                                      .isTranslucent = !areRoomsHidden(view),
                                                      .tintedSurfaceType = view.tintedSurfaceType}),
                         .image = content.images.size()};
    content.images.push_back(std::make_shared<const z64::MemoryImage>(added.drawing.image));
    labelImages(content, "Collision", viewport::ImageKind::Generated);
    return added;
}

void addPolygons(viewport::ViewportContent& content, const AddedCollision& added,
                 const SceneView& view) {
    content.draws.push_back(
        viewport::ViewportDraw{.image = added.image, .displayList = added.drawing.setup});
    const std::vector<u32>& polygons = added.drawing.polygons;
    for (usize i = 0; i < polygons.size(); ++i) {
        if (polygons[i] != 0) {
            content.draws.push_back(viewport::ViewportDraw{
                .image = added.image,
                .displayList = polygons[i],
                .owner = makeNodeKey(CollisionPolygonRef{.layer = view.layer, .polygon = i}),
                .isEdged = true,
                .isBoxed = false});
        }
    }
}

void addWaterBoxes(viewport::ViewportContent& content, const AddedCollision& added,
                   const Collision& collision, const SceneView& view) {
    if (collision.waterBoxes.empty()) {
        return;
    }
    content.draws.push_back(
        viewport::ViewportDraw{.image = added.image, .displayList = added.drawing.waterBoxSetup});
    for (usize i = 0; i < added.drawing.waterBoxes.size(); ++i) {
        content.draws.push_back(
            viewport::ViewportDraw{.image = added.image,
                                   .displayList = added.drawing.waterBoxes[i],
                                   .owner = makeNodeKey(WaterBoxRef{.layer = view.layer, .box = i}),
                                   .isEdged = true,
                                   .isBoxed = false});
    }
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

viewport::ViewportContent makeSceneContent(const Scene& scene, const SceneView& view,
                                           const core::MarkerShape markerShape,
                                           const SceneLayer* shownLayer,
                                           const Collision* shownCollision) {
    viewport::ViewportContent content{.images = scene.images};
    labelSceneImages(content, scene);
    if (scene.images.empty() || view.layer >= scene.layers.size()) {
        return content;
    }
    const SceneLayer& layer = shownLayer != nullptr ? *shownLayer : scene.layers[view.layer];
    const hackeroot::GameSetup setup = hackeroot::findGameSetup(*scene.images.front());
    const RuntimeSegments runtime = makeRuntimeSegments();
    const Collision* collision =
        shownCollision != nullptr ? shownCollision : findLayerCollision(scene, view.layer);
    const bool showsCollision = collision != nullptr && view.showsCollision;
    const bool showsWaterBoxes = collision != nullptr && view.showsWaterBoxes;
    const bool isCollisionOnly = showsCollision && areRoomsHidden(view);
    const PassSource source{.layer = layer,
                            .view = view,
                            .setup = setup,
                            .imageCount = scene.images.size(),
                            .standInImage = content.images.size(),
                            .standIn = runtime.standIn,
                            .ownsRooms = !showsCollision};
    content.images.push_back(std::make_shared<const z64::MemoryImage>(runtime.image));
    labelImages(content, "Draw config stand-in", viewport::ImageKind::Generated);

    content.draws = makePass(source, true);
    std::optional<AddedCollision> added;
    if (showsCollision || showsWaterBoxes) {
        added = addCollisionImage(content, *collision, view);
    }
    if (isCollisionOnly) {
        addPolygons(content, *added, view);
    }
    std::vector<viewport::ViewportDraw> translucentActors;
    if (view.showsActors) {
        Placed placed = collectPlaced(layer, view);
        if (view.drawsModels) {
            ActorDraws draws = drawModels(scene, view, placed, content.images.size());
            std::ranges::move(draws.images, std::back_inserter(content.images));
            labelActorImages(content, scene.actors);
            std::ranges::move(draws.opaque, std::back_inserter(content.draws));
            translucentActors = std::move(draws.translucent);
        }
        addMarkers(content, makeMarkers(placed), markerShape);
        labelImages(content, "Actor markers", viewport::ImageKind::Generated);
    }
    if (view.showsPaths) {
        addMarkers(content, makePathMarkers(layer, view.layer), markerShape);
        labelImages(content, "Path markers", viewport::ImageKind::Generated);
    }
    std::ranges::move(makePass(source, false), std::back_inserter(content.draws));
    std::ranges::move(translucentActors, std::back_inserter(content.draws));
    if (showsCollision && !isCollisionOnly) {
        addPolygons(content, *added, view);
    }
    if (showsWaterBoxes) {
        addWaterBoxes(content, *added, *collision, view);
    }

    if (view.lightSetting < layer.header.lightSettings.size()) {
        content.lighting = makeLighting(layer.header.lightSettings[view.lightSetting]);
    }
    return content;
}

std::optional<u16> findTintedSurfaceType(const Scene& scene, const SceneView& view,
                                         const SceneNode* active) {
    const auto* polygon = active != nullptr ? std::get_if<CollisionPolygonRef>(active) : nullptr;
    if (polygon == nullptr || polygon->layer != view.layer) {
        return std::nullopt;
    }
    const Collision* collision = findLayerCollision(scene, polygon->layer);
    if (collision == nullptr || polygon->polygon >= collision->polygons.size()) {
        return std::nullopt;
    }
    return collision->polygons[polygon->polygon].type;
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
