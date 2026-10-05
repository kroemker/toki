#include "SceneView.h"

#include "app/scene/Types.h"
#include "app/scene/domain/ActorMarkers.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/Segment.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize SetupDisplayListSize = usize{6} * 8;
constexpr usize SettingsPerDay = 4;
constexpr usize Midday = 1;

struct RoomSetup {
    std::optional<u32> drawConfig;
    std::optional<u32> setupDisplayList;
};

[[nodiscard]] RoomSetup findRoomSetup(const z64::MemoryImage& code) {
    RoomSetup setup{.drawConfig = code.findAddress("sDefaultDisplayList")};
    const std::optional<z64::ImageSymbol> setups = code.findSymbol("sSetupDL");
    if (setups && setups->size >= (RoomSetupDisplayList + 1) * SetupDisplayListSize) {
        setup.setupDisplayList = n64::pack(n64::SegmentAddress{
            .segment = setups->segment,
            .offset =
                static_cast<u32>(setups->offset + (RoomSetupDisplayList * SetupDisplayListSize))});
    }
    return setup;
}

[[nodiscard]] bool isShown(const SceneView& view, const usize room) {
    return room >= view.hiddenRooms.size() || !view.hiddenRooms[room];
}

[[nodiscard]] std::vector<Marker> collectMarkers(const SceneLayer& layer, const SceneView& view) {
    std::vector<Marker> markers;
    for (usize room = 0; room < layer.rooms.size(); ++room) {
        if (!isShown(view, room)) {
            continue;
        }
        for (const ActorPlacement& actor : layer.rooms[room].actors) {
            markers.push_back(Marker{.position = actor.position, .kind = MarkerKind::Actor});
        }
    }
    for (const ActorPlacement& entry : layer.header.playerEntries) {
        markers.push_back(Marker{.position = entry.position, .kind = MarkerKind::PlayerEntry});
    }
    for (const TransitionActorPlacement& actor : layer.header.transitionActors) {
        markers.push_back(Marker{.position = actor.position, .kind = MarkerKind::TransitionActor});
    }
    return markers;
}

struct PassSource {
    const SceneLayer& layer;
    const SceneView& view;
    const RoomSetup& setup;
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

viewport::ViewportContent makeSceneContent(const Scene& scene, const SceneView& view) {
    viewport::ViewportContent content{.images = scene.images};
    if (scene.images.empty() || view.layer >= scene.layers.size()) {
        return content;
    }
    const SceneLayer& layer = scene.layers[view.layer];
    const RoomSetup setup = findRoomSetup(*scene.images.front());
    const PassSource source{
        .layer = layer, .view = view, .setup = setup, .imageCount = scene.images.size()};

    content.draws = makePass(source, true);
    if (view.showsActors) {
        const std::vector<Marker> markers = collectMarkers(layer, view);
        if (!markers.empty()) {
            ActorMarkers made = makeActorMarkers(markers);
            content.draws.push_back(viewport::ViewportDraw{.image = content.images.size(),
                                                           .displayList = made.displayList});
            content.images.push_back(
                std::make_shared<const z64::MemoryImage>(std::move(made.image)));
        }
    }
    std::ranges::move(makePass(source, false), std::back_inserter(content.draws));

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
