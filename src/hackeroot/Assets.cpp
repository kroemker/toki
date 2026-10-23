#include "Assets.h"

#include "base/Diagnostic.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/Types.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr usize MaxSceneDepth = 3;

[[nodiscard]] std::vector<std::filesystem::directory_entry>
listDirectory(const std::filesystem::path& directory) {
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(directory, failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        entries.push_back(*entry);
    }
    return entries;
}

[[nodiscard]] bool isFolder(const std::filesystem::directory_entry& entry) {
    std::error_code failure;
    return entry.is_directory(failure);
}

[[nodiscard]] bool holdsSceneFile(const std::vector<std::filesystem::directory_entry>& entries,
                                  const std::string& name) {
    const std::string prefix = name + "_scene";
    return std::ranges::any_of(entries, [&prefix](const std::filesystem::directory_entry& entry) {
        return !isFolder(entry) && entry.path().filename().string().starts_with(prefix);
    });
}

/// Whether the scene `name` in `directory` was written by Toki, which puts its
/// marker on the first line of the scene's main file. Only that line is read.
[[nodiscard]] bool isWrittenByToki(const std::filesystem::path& directory,
                                   const std::string& name) {
    std::ifstream file(directory / (name + "_scene_main.c"));
    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    while (line.ends_with('')) {
        line.pop_back();
}

return line == makeTokiMarker("scene");
}

[[nodiscard]] usize countRooms(const std::vector<std::filesystem::directory_entry>& entries,
                               const std::string& name) {
    const std::string prefix = name + "_room_";
    std::set<std::string> rooms;
    for (const std::filesystem::directory_entry& entry : entries) {
        const std::string file = entry.path().filename().string();
        if (isFolder(entry) || !file.starts_with(prefix)) {
            continue;
        }
        const usize digits = file.find_first_not_of("0123456789", prefix.size());
        if (digits > prefix.size()) {
            rooms.insert(file.substr(prefix.size(), digits - prefix.size()));
        }
    }
    return rooms.size();
}

[[nodiscard]] std::vector<AssetDirectory>
collectScenes(const std::vector<std::filesystem::directory_entry>& entries, usize depth,
              bool isExtracted) {
    std::vector<AssetDirectory> scenes;
    for (const std::filesystem::directory_entry& entry : entries) {
        if (!isFolder(entry)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const std::vector<std::filesystem::directory_entry> inside = listDirectory(entry.path());
        if (holdsSceneFile(inside, name)) {
            scenes.push_back(AssetDirectory{.name = name,
                                            .directory = entry.path(),
                                            .isExtracted = isExtracted,
                                            .isWrittenByToki =
                                                !isExtracted && isWrittenByToki(entry.path(), name),
                                            .roomCount = countRooms(inside, name)});
        } else if (depth + 1 < MaxSceneDepth) {
            std::vector<AssetDirectory> deeper = collectScenes(inside, depth + 1, isExtracted);
            scenes.insert(scenes.end(), std::make_move_iterator(deeper.begin()),
                          std::make_move_iterator(deeper.end()));
        }
    }
    return scenes;
}

[[nodiscard]] std::vector<AssetDirectory> collectObjects(const std::filesystem::path& directory,
                                                         bool isExtracted) {
    std::vector<AssetDirectory> objects;
    for (const std::filesystem::directory_entry& entry : listDirectory(directory)) {
        if (isFolder(entry)) {
            objects.push_back(AssetDirectory{.name = entry.path().filename().string(),
                                             .directory = entry.path(),
                                             .isExtracted = isExtracted});
        }
    }
    return objects;
}

[[nodiscard]] std::vector<AssetDirectory> mergeByName(std::vector<AssetDirectory> first,
                                                      std::vector<AssetDirectory> second) {
    first.insert(first.end(), std::make_move_iterator(second.begin()),
                 std::make_move_iterator(second.end()));
    std::ranges::sort(first, [](const AssetDirectory& left, const AssetDirectory& right) {
        return left.name != right.name ? left.name < right.name
                                       : left.isExtracted < right.isExtracted;
    });
    return first;
}

/// Drops each extracted scene the checkout's `assets/` holds a scene of the
/// same name for: a vanilla scene Toki saved is copied there, and the spec
/// links the copy from then on.
[[nodiscard]] std::vector<AssetDirectory> dropShadowed(std::vector<AssetDirectory> merged) {
    std::erase_if(merged, [&merged](const AssetDirectory& asset) {
        return asset.isExtracted &&
               std::ranges::any_of(merged, [&asset](const AssetDirectory& other) {
                   return !other.isExtracted && other.name == asset.name;
               });
    });
    return merged;
}

} // namespace

Assets listAssets(const std::filesystem::path& repository, std::string_view version) {
    Assets assets;
    std::vector<AssetDirectory> extractedScenes;
    std::vector<AssetDirectory> extractedObjects;

    const std::filesystem::path extracted = repository / "extracted" / version / "assets";
    std::error_code failure;
    if (version.empty() || !std::filesystem::is_directory(extracted, failure)) {
        assets.diagnostics.push_back(base::makeWarning(
            base::SourceLocation{.file = extracted},
            std::format("no extracted assets for version '{}'; run make setup for it", version)));
    } else {
        extractedScenes = collectScenes(listDirectory(extracted / "scenes"), 0, true);
        extractedObjects = collectObjects(extracted / "objects", true);
    }

    assets.scenes = dropShadowed(
        mergeByName(collectScenes(listDirectory(repository / "assets/scenes"), 0, false),
                    std::move(extractedScenes)));
    assets.objects = mergeByName(collectObjects(repository / "assets/objects", false),
                                 std::move(extractedObjects));
    for (const std::filesystem::directory_entry& entry : listDirectory(repository / "extracted")) {
        if (isFolder(entry)) {
            assets.versions.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(assets.versions);
    return assets;
}

} // namespace hackeroot
