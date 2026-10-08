#include "hackeroot/AssetImage.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"
#include "n64/Memory.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using base::u32;
using base::u8;

namespace {

std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

z64::MemoryImage readRoom() {
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(getCheckout(), {});
    REQUIRE(workspace.has_value());
    const std::array segments = {hackeroot::SegmentFiles{
        .segment = 3, .files = {getCheckout() / "assets/scenes/custom/custom_room_0.c"}}};
    return hackeroot::readAssetImage(*workspace, segments);
}

std::filesystem::path makeWatchedFile() {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "toki_tests" / "shared_images";
    std::error_code failure;
    std::filesystem::create_directories(folder, failure);
    const std::filesystem::path file = folder / "watched.txt";
    std::ofstream(file) << "watched";
    return file;
}

void touch(const std::filesystem::path& file) {
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) +
                                               std::chrono::seconds(10));
}

} // namespace

TEST_CASE("a fast64 room becomes a segment with its addresses patched", "[hackeroot][image]") {
    const z64::MemoryImage image = readRoom();

    std::string listed;
    for (const base::Diagnostic& diagnostic : image.diagnostics) {
        listed += diagnostic.message + "\n";
    }
    INFO(listed);
    REQUIRE(image.diagnostics.empty());
    const z64::ImageSegment* room = image.findSegment(3);
    REQUIRE(room != nullptr);
    REQUIRE(room->bytes == std::vector<u8>{
                               0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00,
                               0x05, 0x06, 0x07, 0x08, 0x09, 0x01, 0x00, 0x10, 0x02, 0x03, 0x00,
                               0x00, 0x00, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
                               0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                               0x03, 0x00, 0x00, 0x20, 0x03, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                           });
    REQUIRE(image.findAddress("custom_room_0DL") == std::optional<u32>(0x03000010));
}

TEST_CASE("a memory image maps where the segment table says", "[hackeroot][image]") {
    const z64::MemoryImage image = readRoom();
    n64::Memory memory;

    const auto table = z64::mapImage(image, memory);

    REQUIRE(table.has_value());
    const std::optional<std::span<const u8>> command = memory.findBytes((*table)[3] + 0x18, 4);
    REQUIRE(command.has_value());
    REQUIRE((*command)[0] == 0xDF);
}

TEST_CASE("a shared image is read once until a file it watches changes", "[hackeroot][image]") {
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(getCheckout(), {});
    REQUIRE(loaded.has_value());
    hackeroot::AssetImageReader reader(
        std::make_shared<const hackeroot::Workspace>(std::move(*loaded)));
    const std::filesystem::path keepWatched = makeWatchedFile();
    const std::filesystem::path keep =
        getCheckout() / "extracted/ntsc-1.2/assets/objects/gameplay_keep/gameplay_keep.c";
    const std::filesystem::path room = getCheckout() / "assets/scenes/custom/custom_room_0.c";
    const std::array shared = {
        hackeroot::SharedImageFiles{.files = {{.segment = 4, .files = {keep}}},
                                    .watched = {keepWatched}},
        hackeroot::SharedImageFiles{.files = {{.segment = 3, .files = {room}}}, .watched = {}},
    };
    const std::array<std::vector<hackeroot::SegmentFiles>, 1> images{
        {{{.segment = 6,
           .files = {getCheckout() / "assets/objects/object_custom/object_custom.c"}}}}};

    const hackeroot::ImageSet first = reader.read(shared, images);
    REQUIRE(first.shared.size() == 2);
    REQUIRE(first.images.size() == 1);
    CHECK(first.shared[0]->findAddress("gIdentityMtx").has_value());
    CHECK_FALSE(first.images[0].findAddress("gIdentityMtx").has_value());
    CHECK(first.shared[1]->findAddress("custom_room_0DL") == std::optional<u32>(0x03000010));

    const hackeroot::ImageSet again = reader.read(shared, images);
    CHECK(again.shared[0] == first.shared[0]);
    CHECK(again.shared[1] == first.shared[1]);

    touch(keepWatched);
    const hackeroot::ImageSet changed = reader.read(shared, images);
    CHECK(changed.shared[0] != first.shared[0]);
    CHECK(changed.shared[1] != first.shared[1]);
    CHECK(changed.shared[1]->findAddress("custom_room_0DL") == std::optional<u32>(0x03000010));

    std::error_code failure;
    std::filesystem::remove_all(keepWatched.parent_path(), failure);
}

TEST_CASE("readers of one workspace share its images", "[hackeroot][image]") {
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(getCheckout(), {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    const std::array shared = {hackeroot::SharedImageFiles{
        .files = {{.segment = 3,
                   .files = {getCheckout() / "assets/scenes/custom/custom_room_0.c"}}},
        .watched = {}}};
    hackeroot::AssetImageReader first(workspace);
    hackeroot::AssetImageReader second(workspace);

    const hackeroot::ImageSet one = first.read(shared, {});
    const hackeroot::ImageSet other = second.read(shared, {});

    REQUIRE(one.shared.front() == other.shared.front());
}
