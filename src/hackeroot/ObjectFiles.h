#pragma once

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Types.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace hackeroot {

class Workspace;

/// Where the game loads an actor's object.
inline constexpr u8 ObjectSegment = 0x06;

/// Where the game keeps `gameplay_keep`, which most objects point into.
inline constexpr u8 KeepSegment = 0x04;

/// The name of the object every play state has loaded in `KeepSegment`.
inline constexpr const char* KeepObjectName = "gameplay_keep";

/// The C files an object's segment is built from, and the headers of the
/// folders they are in.
struct ObjectFiles {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> headers;
    std::vector<base::Diagnostic> diagnostics;

    /// Every file read, headers included, for watching them for changes.
    [[nodiscard]] std::vector<std::filesystem::path> getAllFiles() const;
};

/// The files of the object in `asset`: the C files the spec links into the
/// segment of its name, found in the checkout's assets or the extracted ones,
/// as `gameplay_keep`'s segment takes `gameplay_hacker_keep` too in HackerOoT.
/// An object the spec does not list is every C file of its folder, with a note
/// saying so. A file the spec names that is in neither is an error.
[[nodiscard]] ObjectFiles findObjectFiles(const Workspace& workspace, const AssetDirectory& asset);

/// The folder of the object called `name`, if the checkout has one.
[[nodiscard]] std::optional<AssetDirectory> findObjectDirectory(const Assets& assets,
                                                                std::string_view name);

/// An object as a shared image in `segment`, watching its files and its folder.
[[nodiscard]] SharedImageFiles makeSharedObject(const Workspace& workspace,
                                                const AssetDirectory& asset, u8 segment);

} // namespace hackeroot
