#include "z64/MemoryImage.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "n64/Memory.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using base::u32;
using base::u8;

namespace {

z64::MemoryImage makeImage(const std::vector<std::pair<u8, std::vector<u8>>>& segments) {
    z64::MemoryImage image;
    for (const auto& [number, bytes] : segments) {
        image.segments.push_back(z64::ImageSegment{.number = number, .bytes = bytes});
    }
    return image;
}

} // namespace

TEST_CASE("segment 0 maps at physical 0, the others from where they are asked to", "[z64][image]") {
    const z64::MemoryImage code = makeImage({{0, {1, 2, 3, 4}}});
    const z64::MemoryImage room = makeImage({{3, {5, 6, 7, 8}}});
    n64::Memory memory;

    const std::expected<z64::MappedImage, base::Diagnostic> first =
        z64::mapImageAt(code, memory, 0x0010'0000);
    REQUIRE(first.has_value());
    const std::expected<z64::MappedImage, base::Diagnostic> second =
        z64::mapImageAt(room, memory, first->end);
    REQUIRE(second.has_value());

    REQUIRE(first->segments[0] == 0);
    REQUIRE(memory.findBytes(0, 4).has_value());
    REQUIRE((*memory.findBytes(0, 4))[3] == 4);
    REQUIRE(second->segments[3] >= 0x0010'0000);
    REQUIRE((*memory.findBytes(second->segments[3], 4))[0] == 5);
}

TEST_CASE("bytes are found only where one segment holds all of them", "[z64][image]") {
    const z64::MemoryImage image = makeImage({{2, {1, 2, 3, 4, 5, 6, 7, 8}}});

    REQUIRE(image.findBytes(0x0200'0004, 4).has_value());
    REQUIRE((*image.findBytes(0x0200'0004, 4))[0] == 5);
    REQUIRE_FALSE(image.findBytes(0x0200'0006, 4).has_value());
    REQUIRE_FALSE(image.findBytes(0x0300'0000, 1).has_value());
    REQUIRE(image.findBytes(0x0200'0008, 0).has_value());
}
