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
#include <chrono>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::object {

namespace {

[[nodiscard]] std::optional<hackeroot::AssetDirectory>
findKeep(const hackeroot::Workspace& workspace) {
    const std::vector<hackeroot::AssetDirectory>& objects = workspace.getAssets().objects;
    const auto keep = std::ranges::find(objects, std::string(hackeroot::KeepObjectName),
                                        &hackeroot::AssetDirectory::name);
    if (keep == objects.end()) {
        return std::nullopt;
    }
    return *keep;
}

} // namespace

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

    std::vector<std::vector<hackeroot::SegmentFiles>> imageFiles{
        {hackeroot::findGameCodeFiles(reader.getWorkspace().getRoot())}};
    const std::optional<hackeroot::AssetDirectory> keep =
        isKeep ? std::nullopt : findKeep(reader.getWorkspace());
    if (keep) {
        imageFiles.push_back({hackeroot::SegmentFiles{
            .segment = hackeroot::KeepSegment, .files = hackeroot::findObjectFiles(*keep).files}});
    }
    imageFiles.push_back(
        {hackeroot::SegmentFiles{.segment = object.segment, .files = files.files}});

    std::vector<z64::MemoryImage> images = reader.read(imageFiles);
    object.objectImage = images.size() - 1;
    for (usize i = 0; i < images.size(); ++i) {
        if (i == object.objectImage) {
            std::ranges::move(images[i].diagnostics, std::back_inserter(object.diagnostics));
        }
        images[i].diagnostics.clear();
        object.images.push_back(std::make_shared<const z64::MemoryImage>(std::move(images[i])));
    }
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
    if (const std::optional<hackeroot::AssetDirectory> keep = findKeep(workspace)) {
        std::ranges::move(hackeroot::findObjectFiles(*keep).getAllFiles(),
                          std::back_inserter(watched));
        watched.push_back(keep->directory);
    }
    return watched;
}

} // namespace toki::object
