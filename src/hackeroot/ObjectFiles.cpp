#include "ObjectFiles.h"

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Spec.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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

std::optional<AssetDirectory> findDrawnDirectory(const Workspace& workspace,
                                                 const std::string_view name) {
    if (!name.starts_with(OverlayPrefix)) {
        return findObjectDirectory(workspace.getAssets(), name);
    }
    const std::expected<OverlayFiles, base::Diagnostic> overlay =
        findOverlayFiles(workspace.getRoot(), name.substr(OverlayPrefix.size()));
    if (!overlay) {
        return std::nullopt;
    }
    return AssetDirectory{.name = std::string(name), .directory = overlay->directory};
}

SharedImageFiles makeSharedObject(const Workspace& workspace, const AssetDirectory& asset,
                                  const u8 segment) {
    if (asset.name.starts_with(OverlayPrefix)) {
        const std::expected<OverlayFiles, base::Diagnostic> overlay = findOverlayFiles(
            workspace.getRoot(), std::string_view(asset.name).substr(OverlayPrefix.size()));
        if (!overlay) {
            return SharedImageFiles{};
        }
        return SharedImageFiles{.files = {SegmentFiles{.segment = segment,
                                                       .files = {overlay->main},
                                                       .isOverlay = true}},
                                .watched = overlay->getAllFiles()};
    }
    ObjectFiles files = findObjectFiles(workspace, asset);
    std::vector<std::filesystem::path> watched = files.getAllFiles();
    watched.push_back(asset.directory);
    return SharedImageFiles{
        .files = {SegmentFiles{.segment = segment, .files = std::move(files.files)}},
        .watched = std::move(watched)};
}

std::vector<std::string> findLinkedObjects(const std::vector<std::filesystem::path>& files,
                                           const std::string_view own) {
    constexpr std::string_view Include = "#include \"";
    constexpr std::string_view Objects = "assets/objects/";
    std::vector<std::string> linked;
    for (const std::filesystem::path& file : files) {
        std::ifstream stream(file);
        std::string line;
        while (std::getline(stream, line)) {
            const usize start = line.find(Objects);
            if (!line.starts_with(Include) || start == std::string::npos) {
                continue;
            }
            const usize end = line.find('/', start + Objects.size());
            if (end == std::string::npos) {
                continue;
            }
            std::string name = line.substr(start + Objects.size(), end - start - Objects.size());
            if (name != own && name != KeepObjectName && !std::ranges::contains(linked, name)) {
                linked.push_back(std::move(name));
            }
        }
    }
    return linked;
}

std::vector<SharedImageFiles> makeLinkedObjects(const Workspace& workspace,
                                                const std::vector<std::filesystem::path>& files,
                                                const std::string_view own) {
    std::vector<SharedImageFiles> shared;
    for (const std::string& name : findLinkedObjects(files, own)) {
        const SpecSegment* linked = workspace.getSpec().findSegment(name);
        const std::optional<AssetDirectory> directory =
            findObjectDirectory(workspace.getAssets(), name);
        if (linked != nullptr && linked->number && directory) {
            shared.push_back(makeSharedObject(workspace, *directory, *linked->number));
        }
    }
    return shared;
}

std::vector<SharedImageFiles> makeObjectImageFiles(const Workspace& workspace,
                                                   const AssetDirectory& asset, const u8 segment) {
    std::vector<SharedImageFiles> shared{makeSharedGameCode(workspace.getRoot())};
    if (asset.name != KeepObjectName) {
        if (const std::optional<AssetDirectory> keep =
                findObjectDirectory(workspace.getAssets(), KeepObjectName)) {
            shared.push_back(makeSharedObject(workspace, *keep, KeepSegment));
        }
    }
    if (!asset.name.starts_with(OverlayPrefix)) {
        std::ranges::move(
            makeLinkedObjects(workspace, findObjectFiles(workspace, asset).files, asset.name),
            std::back_inserter(shared));
    }
    shared.push_back(makeSharedObject(workspace, asset, segment));
    return shared;
}

} // namespace hackeroot
