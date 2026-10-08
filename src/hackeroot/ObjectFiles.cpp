#include "ObjectFiles.h"

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Spec.h"
#include "hackeroot/Workspace.h"

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

namespace {

void listHeaders(const std::filesystem::path& folder, std::vector<std::filesystem::path>& headers) {
    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(folder, failure)) {
        if (entry.path().extension() == ".h" && !std::ranges::contains(headers, entry.path())) {
            headers.push_back(entry.path());
        }
    }
}

[[nodiscard]] ObjectFiles findFolderFiles(const AssetDirectory& asset) {
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

[[nodiscard]] std::optional<std::filesystem::path> findSource(const Workspace& workspace,
                                                              const std::string_view include) {
    std::filesystem::path source(include);
    source.replace_extension(".c");
    const std::filesystem::path& root = workspace.getRoot();
    std::error_code failure;
    for (const std::filesystem::path& candidate :
         {root / source, root / "extracted" / workspace.getBuildDefines().version / source}) {
        if (std::filesystem::is_regular_file(candidate, failure)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace

ObjectFiles findObjectFiles(const Workspace& workspace, const AssetDirectory& asset) {
    const SpecSegment* segment = workspace.getSpec().findSegment(asset.name);
    if (segment == nullptr) {
        ObjectFiles found = findFolderFiles(asset);
        found.diagnostics.push_back(base::makeNote(
            {.file = asset.directory},
            std::format("the spec has no segment {}, so every C file of its folder is read",
                        asset.name)));
        return found;
    }
    ObjectFiles found;
    for (const std::string& include : segment->includes) {
        if (!include.starts_with("assets/") || !include.ends_with(".o")) {
            continue;
        }
        if (const std::optional<std::filesystem::path> source = findSource(workspace, include)) {
            found.files.push_back(*source);
            listHeaders(source->parent_path(), found.headers);
        } else {
            found.diagnostics.push_back(base::makeError(
                {.file = workspace.getRoot() / "spec" / "spec"},
                std::format("the spec links {} into {}, but no C file of it was found", include,
                            asset.name)));
        }
    }
    std::ranges::sort(found.headers);
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

SharedImageFiles makeSharedObject(const Workspace& workspace, const AssetDirectory& asset,
                                  const u8 segment) {
    ObjectFiles files = findObjectFiles(workspace, asset);
    std::vector<std::filesystem::path> watched = files.getAllFiles();
    watched.push_back(asset.directory);
    return SharedImageFiles{
        .files = {SegmentFiles{.segment = segment, .files = std::move(files.files)}},
        .watched = std::move(watched)};
}

} // namespace hackeroot
