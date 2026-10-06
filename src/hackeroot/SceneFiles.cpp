#include "SceneFiles.h"

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Types.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

struct Named {
    std::filesystem::path path;
    std::string stem;
};

[[nodiscard]] std::optional<usize> findRoomNumber(const std::string_view stem,
                                                  const std::string_view prefix) {
    if (!stem.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view rest = stem.substr(prefix.size());
    usize number = 0;
    const auto [end, failure] = std::from_chars(rest.data(), rest.data() + rest.size(), number);
    if (failure != std::errc{} || end == rest.data() ||
        (end != rest.data() + rest.size() && *end != '_')) {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] std::vector<std::filesystem::path> orderFiles(std::vector<Named> files,
                                                            const std::string& header) {
    std::ranges::sort(files, [&header](const Named& first, const Named& second) {
        const bool firstIsHeader = first.stem == header || first.stem == header + "_main";
        const bool secondIsHeader = second.stem == header || second.stem == header + "_main";
        if (firstIsHeader != secondIsHeader) {
            return firstIsHeader;
        }
        return first.stem < second.stem;
    });
    std::vector<std::filesystem::path> paths;
    paths.reserve(files.size());
    for (Named& file : files) {
        paths.push_back(std::move(file.path));
    }
    return paths;
}

} // namespace

std::vector<std::vector<SegmentFiles>> SceneFiles::makeImageFiles() const {
    std::vector<std::vector<SegmentFiles>> images;
    images.push_back({SegmentFiles{.segment = SceneSegment, .files = scene}});
    for (const std::vector<std::filesystem::path>& room : rooms) {
        images.push_back({SegmentFiles{.segment = RoomSegment, .files = room}});
    }
    return images;
}

std::vector<std::filesystem::path> SceneFiles::getAllFiles() const {
    std::vector<std::filesystem::path> files = scene;
    for (const std::vector<std::filesystem::path>& room : rooms) {
        files.insert(files.end(), room.begin(), room.end());
    }
    files.insert(files.end(), headers.begin(), headers.end());
    return files;
}

SceneFiles findSceneFiles(const AssetDirectory& asset) {
    SceneFiles found;
    const std::string sceneHeader = asset.name + "_scene";
    const std::string roomPrefix = asset.name + "_room_";
    std::vector<Named> sceneFiles;
    std::map<usize, std::vector<Named>> roomFiles;

    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(asset.directory, failure)) {
        const std::string name = entry.path().filename().string();
        if (name.ends_with(".h")) {
            found.headers.push_back(entry.path());
            continue;
        }
        if (!name.ends_with(".c") || name.ends_with(".inc.c")) {
            continue;
        }
        std::string stem = name.substr(0, name.size() - 2);
        if (stem == sceneHeader || stem.starts_with(sceneHeader + "_")) {
            sceneFiles.push_back(Named{.path = entry.path(), .stem = std::move(stem)});
        } else if (const std::optional<usize> number = findRoomNumber(stem, roomPrefix)) {
            roomFiles[*number].push_back(Named{.path = entry.path(), .stem = std::move(stem)});
        }
    }
    if (failure) {
        found.diagnostics.push_back(base::makeError(
            {.file = asset.directory},
            std::format("the scene folder could not be listed: {}", failure.message())));
        return found;
    }
    if (sceneFiles.empty()) {
        found.diagnostics.push_back(base::makeError(
            {.file = asset.directory}, std::format("no {}.c in the scene folder", sceneHeader)));
    }
    found.scene = orderFiles(std::move(sceneFiles), sceneHeader);

    usize expected = 0;
    for (auto& [number, files] : roomFiles) {
        if (number != expected) {
            found.diagnostics.push_back(base::makeError(
                {.file = asset.directory},
                std::format("room {} has no files, so the rooms after it are not read", expected)));
            break;
        }
        found.rooms.push_back(
            orderFiles(std::move(files), std::format("{}{}", roomPrefix, number)));
        ++expected;
    }
    return found;
}

} // namespace hackeroot
