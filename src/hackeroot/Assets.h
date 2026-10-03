#pragma once

#include "base/Diagnostic.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

/// One scene or object folder. `isExtracted` tells a vanilla asset, extracted
/// from the ROM under `extracted/<version>/`, from one in the checkout's own
/// `assets/`, which is where fast64 exports go.
struct AssetDirectory {
    std::string name;
    std::filesystem::path directory;
    bool isExtracted = false;
};

struct Assets {
    std::vector<AssetDirectory> scenes;
    std::vector<AssetDirectory> objects;
    std::vector<base::Diagnostic> diagnostics;
};

/// Lists the scene and object folders of a checkout, from `assets/` and from
/// `extracted/<version>/assets/`, each sorted by name. A scene folder is one
/// holding a file named after it (`spot00/spot00_scene.c`,
/// `oceantest/oceantest_scene.h`), at any depth under `scenes/`; an object
/// folder is any folder directly under `objects/`. A missing `extracted/`
/// version is a warning: the checkout has not been set up for that version.
[[nodiscard]] Assets listAssets(const std::filesystem::path& repository, std::string_view version);

} // namespace hackeroot
