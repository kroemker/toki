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

/// Where the game loads an actor's object.
inline constexpr u8 ObjectSegment = 0x06;

/// Where the game keeps `gameplay_keep`, which most objects point into.
inline constexpr u8 KeepSegment = 0x04;

/// The name of the object every play state has loaded in `KeepSegment`.
inline constexpr const char* KeepObjectName = "gameplay_keep";

/// The C files an object folder's segment is built from, by name, the way the
/// spec lists `object_<name>.c` and whatever else the folder's own C is split
/// into, and the folder's headers.
struct ObjectFiles {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> headers;
    std::vector<base::Diagnostic> diagnostics;

    /// Every file read, headers included, for watching them for changes.
    [[nodiscard]] std::vector<std::filesystem::path> getAllFiles() const;
};

[[nodiscard]] ObjectFiles findObjectFiles(const AssetDirectory& asset);

/// The folder of the object called `name`, if the checkout has one.
[[nodiscard]] std::optional<AssetDirectory> findObjectDirectory(const Assets& assets,
                                                                std::string_view name);

/// An object as a shared image in `segment`, watching its files and its folder.
[[nodiscard]] SharedImageFiles makeSharedObject(const AssetDirectory& asset, u8 segment);

} // namespace hackeroot
