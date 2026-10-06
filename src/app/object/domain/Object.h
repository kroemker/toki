#pragma once

#include "app/object/Types.h"
#include "app/object/domain/ObjectAssets.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace toki::object {

/// An object read from its folder, as the game has it loaded for an actor:
/// the game code the actor is drawn with, `gameplay_keep`, which most objects
/// point into, and the object itself, with every asset it holds.
struct Object {
    std::string name;
    std::filesystem::path directory;
    /// The segment the object is in: 6, or 4 for `gameplay_keep` itself.
    u8 segment = 0;
    /// Shared with what the viewport draws of the object, which is made again
    /// whenever the selection or the frame changes; an image never changes.
    std::vector<std::shared_ptr<const z64::MemoryImage>> images;
    /// Which of `images` the object is.
    usize objectImage = 0;
    std::vector<ObjectAsset> assets;
    std::vector<std::filesystem::path> files;
    std::vector<base::Diagnostic> diagnostics;
    std::chrono::milliseconds elapsed{};
};

/// Reads an object, with `gameplay_keep` from the same checkout beside it.
[[nodiscard]] Object readObject(hackeroot::AssetImageReader& reader,
                                const hackeroot::AssetDirectory& asset);

/// The files and folders `readObject` reads `asset` from, its own and those of
/// `gameplay_keep` beside it, so an export of either reads it again.
[[nodiscard]] std::vector<std::filesystem::path>
listObjectWatched(const hackeroot::Workspace& workspace, const hackeroot::AssetDirectory& asset);

} // namespace toki::object
