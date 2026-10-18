#include "z64/ImageDifference.h"

#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using base::u8;

namespace {

z64::ImageObject makeObject(const std::string& name, std::vector<u8> bytes,
                            std::vector<z64::Relocation> relocations = {},
                            bool startsFile = false) {
    return z64::ImageObject{
        .segment = 2,
        .name = name,
        .type = "u32",
        .isArray = true,
        .alignment = 4,
        .startsFile = startsFile,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)},
    };
}

z64::MemoryImage makeImage(const bool isReordered) {
    std::vector<z64::ImageObject> objects;
    objects.push_back(makeObject("a", {0, 0, 0, 1}));
    objects.push_back(makeObject("b", {0, 0, 0, 4}, {{.offset = 0, .symbol = "a"}}, isReordered));
    if (isReordered) {
        std::swap(objects[0], objects[1]);
    }
    return z64::layOutImage(std::move(objects), {});
}

} // namespace

TEST_CASE("the same symbols laid out elsewhere do not differ", "[z64][image]") {
    const z64::MemoryImage expected = makeImage(false);
    const z64::MemoryImage actual = makeImage(true);

    REQUIRE(expected.segments.front().bytes != actual.segments.front().bytes);
    REQUIRE(z64::compareSymbols(expected, actual, 2).empty());
}

TEST_CASE("symbols differ by bytes, by what they point at, and by being there", "[z64][image]") {
    const z64::MemoryImage expected = makeImage(false);
    std::vector<z64::ImageObject> objects;
    objects.push_back(makeObject("a", {0, 0, 0, 2}));
    objects.push_back(makeObject("b", {0, 0, 0, 4}, {{.offset = 0, .symbol = "c"}}));
    objects.push_back(makeObject("c", {0, 0, 0, 0}));
    const z64::MemoryImage actual = z64::layOutImage(std::move(objects), {});

    const std::vector<z64::ImageDifference> differences = z64::compareSymbols(expected, actual, 2);

    REQUIRE(differences.size() == 3);
    REQUIRE(differences[0].symbol == "a");
    REQUIRE(differences[0].message == "byte 0x3 is 0x02, not 0x01");
    REQUIRE(differences[1].symbol == "b");
    REQUIRE(differences[1].message == "points at 'c' from 0x0, not at 'a' from 0x0");
    REQUIRE(differences[2].symbol == "c");
    REQUIRE(differences[2].message == "is new");
}
