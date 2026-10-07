#include "Object.h"

#include "app/object/Types.h"
#include "app/object/domain/ObjectAssets.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::object {

Object readObject(hackeroot::AssetImageReader& reader, const hackeroot::AssetDirectory& asset) {
    const auto started = std::chrono::steady_clock::now();
    Object object{.name = asset.name, .directory = asset.directory};
    const bool isKeep = asset.name == hackeroot::KeepObjectName;
    object.segment = isKeep ? hackeroot::KeepSegment : hackeroot::ObjectSegment;

    hackeroot::ObjectFiles files = hackeroot::findObjectFiles(asset);
    object.files = files.getAllFiles();
    object.diagnostics = std::move(files.diagnostics);
    if (files.files.empty()) {
        return object;
    }

    std::vector<hackeroot::SharedImageFiles> shared{
        hackeroot::makeSharedGameCode(reader.getWorkspace().getRoot())};
    const std::optional<hackeroot::AssetDirectory> keep =
        isKeep ? std::nullopt
               : hackeroot::findObjectDirectory(reader.getWorkspace().getAssets(),
                                                hackeroot::KeepObjectName);
    if (keep) {
        shared.push_back(hackeroot::makeSharedObject(*keep, hackeroot::KeepSegment));
    }
    const std::array<std::vector<hackeroot::SegmentFiles>, 1> imageFiles{
        {{{.segment = object.segment, .files = files.files}}}};

    hackeroot::ImageSet images = reader.read(shared, imageFiles);
    object.images = std::move(images.shared);
    object.objectImage = object.images.size();
    z64::MemoryImage& objectImage = images.images.front();
    std::ranges::move(objectImage.diagnostics, std::back_inserter(object.diagnostics));
    objectImage.diagnostics.clear();
    object.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(objectImage)));
    object.assets = listObjectAssets(*object.images[object.objectImage], object.segment);
    object.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return object;
}

std::vector<std::filesystem::path> listObjectWatched(const hackeroot::Workspace& workspace,
                                                     const hackeroot::AssetDirectory& asset) {
    std::vector<std::filesystem::path> watched = hackeroot::findObjectFiles(asset).getAllFiles();
    if (asset.name == hackeroot::KeepObjectName) {
        return watched;
    }
    if (const std::optional<hackeroot::AssetDirectory> keep =
            hackeroot::findObjectDirectory(workspace.getAssets(), hackeroot::KeepObjectName)) {
        std::ranges::move(hackeroot::findObjectFiles(*keep).getAllFiles(),
                          std::back_inserter(watched));
        watched.push_back(keep->directory);
    }
    return watched;
}

} // namespace toki::object
