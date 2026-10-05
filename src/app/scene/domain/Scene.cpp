#include "Scene.h"

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/SceneFiles.h"
#include "hackeroot/Workspace.h"
#include "n64/Segment.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize SetupDisplayListSize = usize{6} * 8;

[[nodiscard]] std::vector<base::Diagnostic> placeIn(std::vector<base::Diagnostic> found,
                                                    const std::filesystem::path& file) {
    for (base::Diagnostic& diagnostic : found) {
        if (diagnostic.location.file.empty()) {
            diagnostic.location.file = file;
        }
    }
    return found;
}

} // namespace

usize findShownLightSetting(const SceneHeader& header) {
    constexpr usize Day = 1;
    if (header.lightMode == LightMode::Time && Day < header.lightSettings.size()) {
        return Day;
    }
    return 0;
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

viewport::ViewportContent makeSceneContent(std::vector<z64::MemoryImage> images,
                                           const std::vector<RoomHeader>& rooms) {
    constexpr usize FirstRoomImage = 2;
    viewport::ViewportContent content{.images = std::move(images)};
    if (content.images.empty()) {
        return content;
    }
    const z64::MemoryImage& code = content.images.front();
    const std::optional<u32> drawConfig = code.findAddress("sDefaultDisplayList");
    const std::optional<z64::ImageSymbol> setups = code.findSymbol("sSetupDL");
    for (const bool opaque : {true, false}) {
        if (drawConfig) {
            content.draws.push_back(viewport::ViewportDraw{.image = 0, .displayList = *drawConfig});
        }
        if (setups && setups->size >= (RoomSetupDisplayList + 1) * SetupDisplayListSize) {
            content.draws.push_back(viewport::ViewportDraw{
                .image = 0,
                .displayList = n64::pack(n64::SegmentAddress{
                    .segment = setups->segment,
                    .offset = static_cast<u32>(setups->offset +
                                               (RoomSetupDisplayList * SetupDisplayListSize))})});
        }
        for (usize room = 0; room < rooms.size() && room + FirstRoomImage < content.images.size();
             ++room) {
            for (const RoomDisplayLists& lists : rooms[room].displayLists) {
                const u32 displayList = opaque ? lists.opaque : lists.translucent;
                if (displayList != 0) {
                    content.draws.push_back(viewport::ViewportDraw{.image = room + FirstRoomImage,
                                                                   .displayList = displayList});
                }
            }
        }
    }
    return content;
}

Scene readScene(hackeroot::AssetImageReader& reader, const hackeroot::AssetDirectory& asset) {
    const auto started = std::chrono::steady_clock::now();
    Scene scene{.name = asset.name, .directory = asset.directory};
    hackeroot::SceneFiles files = hackeroot::findSceneFiles(asset);
    scene.files = files.getAllFiles();
    scene.diagnostics = std::move(files.diagnostics);
    if (files.scene.empty()) {
        return scene;
    }

    std::vector<std::vector<hackeroot::SegmentFiles>> imageFiles = files.makeImageFiles();
    imageFiles.insert(imageFiles.begin(),
                      {hackeroot::findSceneCodeFiles(reader.getWorkspace().getRoot())});
    std::vector<z64::MemoryImage> images = reader.read(imageFiles);
    images.front().diagnostics.clear();
    for (const char* symbol : {"sSetupDL", "sDefaultDisplayList"}) {
        if (!images.front().findSymbol(symbol)) {
            scene.diagnostics.push_back(base::makeWarning(
                {.file = imageFiles.front().front().files.front().parent_path()},
                std::format("'{}' could not be read from the game's code, so the rooms are drawn "
                            "without it",
                            symbol)));
        }
    }
    for (z64::MemoryImage& image : images) {
        std::ranges::move(image.diagnostics, std::back_inserter(scene.diagnostics));
        image.diagnostics.clear();
    }

    scene.header = readSceneHeader(images[1]);
    std::ranges::move(placeIn(std::exchange(scene.header.diagnostics, {}), files.scene.front()),
                      std::back_inserter(scene.diagnostics));
    for (usize room = 0; room < files.rooms.size(); ++room) {
        RoomHeader header = readRoomHeader(images[room + 2]);
        std::ranges::move(placeIn(std::exchange(header.diagnostics, {}), files.rooms[room].front()),
                          std::back_inserter(scene.diagnostics));
        scene.rooms.push_back(std::move(header));
    }
    if (scene.header.roomCount != files.rooms.size()) {
        scene.diagnostics.push_back(base::makeWarning(
            {.file = files.scene.front()},
            std::format("the scene's room list has {} rooms, but its folder holds {}",
                        scene.header.roomCount, files.rooms.size())));
    }

    scene.content = makeSceneContent(std::move(images), scene.rooms);
    const usize setting = findShownLightSetting(scene.header);
    if (setting < scene.header.lightSettings.size()) {
        scene.content.lighting = makeLighting(scene.header.lightSettings[setting]);
    }
    scene.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return scene;
}

} // namespace toki::scene
