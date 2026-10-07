#include "Scene.h"

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/SceneFiles.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::vector<base::Diagnostic> placeIn(std::vector<base::Diagnostic> found,
                                                    const std::filesystem::path& file) {
    for (base::Diagnostic& diagnostic : found) {
        if (diagnostic.location.file.empty()) {
            diagnostic.location.file = file;
        }
    }
    return found;
}

class LayerReader {
public:
    LayerReader(const Scene& scene, const hackeroot::SceneFiles& files)
        : scene(scene), files(files) {}

    [[nodiscard]] SceneLayer readMainLayer();
    [[nodiscard]] SceneLayer readLayer(usize index, u32 sceneHeader,
                                       const std::vector<RoomHeader>& mainRooms);

    [[nodiscard]] std::vector<base::Diagnostic> takeDiagnostics() {
        return std::move(diagnostics);
    }

private:
    void keep(std::vector<base::Diagnostic> found, const std::filesystem::path& file) {
        std::ranges::move(placeIn(std::move(found), file), std::back_inserter(diagnostics));
    }

    const Scene& scene;
    const hackeroot::SceneFiles& files;
    std::vector<base::Diagnostic> diagnostics;
};

SceneLayer LayerReader::readMainLayer() {
    SceneLayer layer{.index = 0, .header = readSceneHeader(*scene.images[SceneImage])};
    keep(std::exchange(layer.header.diagnostics, {}), files.scene.front());
    for (usize room = 0; room < files.rooms.size(); ++room) {
        RoomHeader header = readRoomHeader(*scene.images[FirstRoomImage + room]);
        keep(std::exchange(header.diagnostics, {}), files.rooms[room].front());
        layer.rooms.push_back(std::move(header));
    }
    return layer;
}

SceneLayer LayerReader::readLayer(const usize index, const u32 sceneHeader,
                                  const std::vector<RoomHeader>& mainRooms) {
    SceneLayer layer{.index = index,
                     .header = readSceneHeader(*scene.images[SceneImage], sceneHeader)};
    keep(std::exchange(layer.header.diagnostics, {}), files.scene.front());
    for (usize room = 0; room < mainRooms.size(); ++room) {
        const std::vector<u32>& alternates = mainRooms[room].alternateHeaders;
        const u32 roomHeader = index <= alternates.size() ? alternates[index - 1] : 0;
        if (roomHeader == 0) {
            layer.rooms.push_back(mainRooms[room]);
            continue;
        }
        RoomHeader header = readRoomHeader(*scene.images[FirstRoomImage + room], roomHeader);
        keep(std::exchange(header.diagnostics, {}), files.rooms[room].front());
        layer.rooms.push_back(std::move(header));
    }
    return layer;
}

} // namespace

std::string makeLayerName(const usize index) {
    constexpr std::array<const char*, 4> Names{"Child day", "Child night", "Adult day",
                                               "Adult night"};
    if (index < Names.size()) {
        return Names[index];
    }
    return std::format("Cutscene {}", index - Names.size());
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

    const std::array<hackeroot::SharedImageFiles, 1> shared{
        hackeroot::makeSharedGameCode(reader.getWorkspace().getRoot())};
    hackeroot::ImageSet images = reader.read(shared, files.makeImageFiles());
    for (const char* symbol : {"sSetupDL", "sDefaultDisplayList"}) {
        if (!images.shared.front()->findSymbol(symbol)) {
            scene.diagnostics.push_back(base::makeWarning(
                {.file = shared.front().watched.front().parent_path()},
                std::format("'{}' could not be read from the game's code, so the rooms are drawn "
                            "without it",
                            symbol)));
        }
    }
    scene.images = std::move(images.shared);
    for (z64::MemoryImage& image : images.images) {
        std::ranges::move(image.diagnostics, std::back_inserter(scene.diagnostics));
        image.diagnostics.clear();
        scene.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    }

    LayerReader layers(scene, files);
    scene.layers.push_back(layers.readMainLayer());
    const std::vector<u32> alternates = scene.layers.front().header.alternateHeaders;
    const std::vector<RoomHeader> mainRooms = scene.layers.front().rooms;
    for (usize index = 1; index <= alternates.size(); ++index) {
        if (alternates[index - 1] != 0) {
            scene.layers.push_back(layers.readLayer(index, alternates[index - 1], mainRooms));
        }
    }
    std::ranges::move(layers.takeDiagnostics(), std::back_inserter(scene.diagnostics));

    if (scene.layers.front().header.roomCount != files.rooms.size()) {
        scene.diagnostics.push_back(base::makeWarning(
            {.file = files.scene.front()},
            std::format("the scene's room list has {} rooms, but its folder holds {}",
                        scene.layers.front().header.roomCount, files.rooms.size())));
    }
    scene.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return scene;
}

} // namespace toki::scene
