#include "app/scene/domain/SceneModel.h"

#include "app/scene/domain/Scene.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using base::u32;
using base::u8;

namespace {

z64::ImageObject makeObject(const u8 segment, std::string name, std::vector<u8> bytes,
                            std::vector<z64::Relocation> relocations = {}) {
    return z64::ImageObject{
        .segment = segment,
        .name = std::move(name),
        .type = "u8",
        .isArray = true,
        .alignment = 4,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)}};
}

std::shared_ptr<const z64::MemoryImage> layOut(std::vector<z64::ImageObject> objects,
                                               const z64::MemoryImage* before = nullptr) {
    return std::make_shared<const z64::MemoryImage>(
        z64::layOutImage(std::move(objects),
                         z64::LayoutOptions{.findExternal = [before](const std::string_view name) {
                             return before != nullptr ? before->findAddress(name) : std::nullopt;
                         }}));
}

/// The game's code, a scene whose header points at its room list, and a room
/// that points into the game's code and the scene.
toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    scene.images.push_back(layOut({makeObject(0, "sSetupDL", {0, 0, 0, 0, 0, 0, 0, 0})}));
    scene.images.push_back(
        layOut({makeObject(2, "header", {0, 0, 0, 0}, {{.offset = 0, .symbol = "rooms"}}),
                makeObject(2, "rooms", {1, 2, 3, 4})}));
    std::vector<z64::ImageObject> room{
        makeObject(3, "dl", {0, 0, 0, 4, 0, 0, 0, 0},
                   {{.offset = 0, .symbol = "sSetupDL"}, {.offset = 4, .symbol = "rooms"}})};
    scene.images.push_back(layOut(std::move(room), scene.images[1].get()));
    return scene;
}

u32 readWord(const z64::MemoryImage& image, const u32 address) {
    const std::span<const u8> bytes = *image.findBytes(address, 4);
    return (u32{bytes[0]} << 24) | (u32{bytes[1]} << 16) | (u32{bytes[2]} << 8) | bytes[3];
}

} // namespace

TEST_CASE("a scene's symbols laid out again make the images it was read into", "[scene][model]") {
    const toki::scene::Scene scene = makeScene();
    const toki::scene::SceneModel model(scene);

    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();

    REQUIRE(images.size() == 3);
    REQUIRE(images[0] == scene.images[0]);
    REQUIRE(images[1]->segments.front().bytes == scene.images[1]->segments.front().bytes);
    REQUIRE(images[2]->segments.front().bytes == scene.images[2]->segments.front().bytes);
    REQUIRE(model.getImages()[1].front().relocations.size() == 2);
    REQUIRE_FALSE(model.isEdited());
}

TEST_CASE("a changed symbol moves what follows it, and what points at it follows",
          "[scene][model]") {
    toki::scene::SceneModel model(makeScene());
    z64::SymbolData header = *model.findSymbol("header");
    header.bytes.resize(8, 0);

    REQUIRE(model.setSymbol(std::move(header)).has_value());
    const std::vector<std::shared_ptr<const z64::MemoryImage>> images = model.layOut();

    REQUIRE(model.isEdited());
    REQUIRE(images[1]->findAddress("rooms") == std::optional<u32>(0x0200'0008));
    REQUIRE(readWord(*images[1], 0x0200'0000) == 0x0200'0008);
    REQUIRE(readWord(*images[2], 0x0300'0000) == 4);
    REQUIRE(readWord(*images[2], 0x0300'0004) == 0x0200'0008);
}

TEST_CASE("symbols are added under a new name and removed once nothing points at them",
          "[scene][model]") {
    toki::scene::SceneModel model(makeScene());

    REQUIRE_FALSE(model.addSymbol(1, z64::SymbolData{.name = "rooms"}).has_value());
    REQUIRE(model.addSymbol(1, z64::SymbolData{.name = "extra", .alignment = 4, .bytes = {9}})
                .has_value());
    REQUIRE(model.findSymbol("extra")->segment == 3);
    REQUIRE(model.layOut()[2]->findAddress("extra") == std::optional<u32>(0x0300'0008));

    const std::expected<void, base::Diagnostic> pointedAt = model.removeSymbol("rooms");
    REQUIRE_FALSE(pointedAt.has_value());
    REQUIRE(pointedAt.error().message.contains("header, dl"));
    REQUIRE(model.removeSymbol("extra").has_value());
    REQUIRE(model.findSymbol("extra") == nullptr);
}
