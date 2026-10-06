#include "app/object/domain/Object.h"
#include "app/scene/domain/Scene.h"
#include "app/texture/domain/TextureCatalog.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"
#include "n64/TileDescriptor.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using base::usize;

namespace {

struct Tally {
    usize textures = 0;
    usize colorIndexed = 0;
    std::vector<std::string> failures;
};

void decodeAll(const z64::MemoryImage& image,
               const std::vector<const z64::MemoryImage*>& paletteImages, Tally& tally) {
    for (const toki::texture::TextureEntry& entry :
         toki::texture::listTextures(image, paletteImages)) {
        ++tally.textures;
        if (entry.texture.format == n64::TextureFormat::ColorIndex) {
            ++tally.colorIndexed;
        }
        const auto decoded = toki::texture::decodeTexture(image, entry);
        if (!decoded) {
            tally.failures.push_back(decoded.error());
        } else if (decoded->note) {
            tally.failures.push_back(entry.name + ": " + *decoded->note);
        }
    }
}

std::string describe(const Tally& tally) {
    std::string described;
    for (usize i = 0; i < std::min<usize>(tally.failures.size(), 20); ++i) {
        described += tally.failures[i] + "\n";
    }
    return described;
}

} // namespace

TEST_CASE("every texture of real objects and a scene decodes with its palette",
          "[texture][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    hackeroot::AssetImageReader reader(workspace);

    for (const std::string name : {"gameplay_keep", "object_kw1", "object_link_boy"}) {
        const auto& objects = workspace->getAssets().objects;
        const auto asset = std::ranges::find(objects, name, &hackeroot::AssetDirectory::name);
        REQUIRE(asset != objects.end());
        const toki::object::Object object = toki::object::readObject(reader, *asset);
        Tally tally;
        decodeAll(*object.images[object.objectImage], {}, tally);
        INFO(name << ":\n" << describe(tally));
        WARN(name << ": " << tally.textures << " textures, " << tally.colorIndexed
                  << " colour-indexed");
        CHECK(tally.textures > 0);
        CHECK(tally.failures.empty());
    }

    const auto& scenes = workspace->getAssets().scenes;
    const auto spot04 =
        std::ranges::find(scenes, std::string("spot04"), &hackeroot::AssetDirectory::name);
    REQUIRE(spot04 != scenes.end());
    const toki::scene::Scene scene = toki::scene::readScene(reader, *spot04);
    std::vector<const z64::MemoryImage*> sceneImages;
    for (usize i = toki::scene::SceneImage; i < scene.images.size(); ++i) {
        sceneImages.push_back(scene.images[i].get());
    }
    Tally tally;
    for (const z64::MemoryImage* image : sceneImages) {
        decodeAll(*image, sceneImages, tally);
    }
    INFO("spot04:\n" << describe(tally));
    WARN("spot04: " << tally.textures << " textures, " << tally.colorIndexed << " colour-indexed");
    CHECK(tally.textures > 0);
    CHECK(tally.failures.empty());
}
