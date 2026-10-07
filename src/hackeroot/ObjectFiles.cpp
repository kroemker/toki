#include "ObjectFiles.h"

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hackeroot {

std::vector<std::filesystem::path> ObjectFiles::getAllFiles() const {
    std::vector<std::filesystem::path> all = files;
    all.insert(all.end(), headers.begin(), headers.end());
    return all;
}

ObjectFiles findObjectFiles(const AssetDirectory& asset) {
    ObjectFiles found;
    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(asset.directory, failure)) {
        const std::string name = entry.path().filename().string();
        if (name.ends_with(".h")) {
            found.headers.push_back(entry.path());
        } else if (name.ends_with(".c") && !name.ends_with(".inc.c")) {
            found.files.push_back(entry.path());
        }
    }
    if (failure) {
        found.diagnostics.push_back(base::makeError(
            {.file = asset.directory},
            std::format("the object folder could not be listed: {}", failure.message())));
        return found;
    }
    std::ranges::sort(found.files);
    std::ranges::sort(found.headers);
    if (found.files.empty()) {
        found.diagnostics.push_back(
            base::makeError({.file = asset.directory},
                            std::format("no C file in the object folder {}", asset.name)));
    }
    return found;
}

std::optional<AssetDirectory> findObjectDirectory(const Assets& assets,
                                                  const std::string_view name) {
    const auto found = std::ranges::find(assets.objects, name, &AssetDirectory::name);
    if (found == assets.objects.end()) {
        return std::nullopt;
    }
    return *found;
}

SharedImageFiles makeSharedObject(const AssetDirectory& asset, const u8 segment) {
    ObjectFiles files = findObjectFiles(asset);
    std::vector<std::filesystem::path> watched = files.getAllFiles();
    watched.push_back(asset.directory);
    return SharedImageFiles{
        .files = {SegmentFiles{.segment = segment, .files = std::move(files.files)}},
        .watched = std::move(watched)};
}

} // namespace hackeroot
