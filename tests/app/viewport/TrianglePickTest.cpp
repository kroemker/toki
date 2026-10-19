#include "app/viewport/domain/TrianglePick.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

using base::f32;
using base::u32;
using base::usize;

namespace {

n64::Triangle makeTriangle(const f32 depth, const u32 address, const f32 w = 1.0F) {
    n64::Triangle triangle;
    const std::array<std::array<f32, 2>, 3> corners{{{-1.0F, -1.0F}, {1.0F, -1.0F}, {0.0F, 1.0F}}};
    for (usize i = 0; i < corners.size(); ++i) {
        triangle.vertices[i].position = {corners[i][0] * w, corners[i][1] * w, depth * w, w};
    }
    triangle.address = address;
    return triangle;
}

} // namespace

TEST_CASE("the triangle picked is the nearest under the point", "[viewport][pick]") {
    n64::RspResult result;
    result.outputs.emplace_back(makeTriangle(0.5F, 0x100));
    result.outputs.emplace_back(makeTriangle(-0.2F, 0x200, 2.0F));
    result.outputs.emplace_back(makeTriangle(-0.9F, 0x300, -1.0F));

    REQUIRE(toki::viewport::findPickedTriangle(result, {0.0F, 0.0F}) == usize{1});
    REQUIRE_FALSE(toki::viewport::findPickedTriangle(result, {0.9F, 0.9F}));
}

TEST_CASE("an output is in the range of the draw that made it", "[viewport][pick]") {
    const std::array<std::optional<toki::viewport::OutputRange>, 3> ranges{
        toki::viewport::OutputRange{.first = 0, .end = 4}, std::nullopt,
        toki::viewport::OutputRange{.first = 4, .end = 9}};
    REQUIRE(toki::viewport::findRangeOf(ranges, 3) == usize{0});
    REQUIRE(toki::viewport::findRangeOf(ranges, 4) == usize{2});
    REQUIRE_FALSE(toki::viewport::findRangeOf(ranges, 9));
}

TEST_CASE("an address is in the image mapped around it", "[viewport][pick]") {
    const std::array<std::optional<z64::MappedImage>, 3> images{
        z64::MappedImage{.start = 0x100, .end = 0x200}, std::nullopt,
        z64::MappedImage{.start = 0x200, .end = 0x300}};

    REQUIRE(toki::viewport::findImageAt(images, 0x1FC) == usize{0});
    REQUIRE(toki::viewport::findImageAt(images, 0x200) == usize{2});
    REQUIRE_FALSE(toki::viewport::findImageAt(images, 0x300));
}
