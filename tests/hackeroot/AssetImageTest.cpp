#include "hackeroot/AssetImage.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"
#include "n64/Memory.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
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
