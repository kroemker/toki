#include "z64/SymbolData.h"

#include "base/Types.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using base::u8;
using base::usize;

namespace {

z64::ImageObject makeObject(const std::string& name, std::vector<u8> bytes,
                            std::vector<z64::Relocation> relocations = {}) {
    return z64::ImageObject{
        .segment = 3,
        .name = name,
        .type = "u32",
        .isArray = true,
        .alignment = 4,
        .data = z64::EncodedData{.bytes = std::move(bytes), .relocations = std::move(relocations)},
    };
}

std::vector<z64::ImageObject> makeObjects() {
    std::vector<z64::ImageObject> objects;
    objects.push_back(makeObject("first", {0, 0, 0, 1}));
    objects.push_back(
        makeObject("list", {0, 0, 0, 0, 0, 0, 0, 8},
                   {{.offset = 0, .symbol = "first"}, {.offset = 4, .symbol = "first"}}));
    objects.push_back(makeObject("outside", {0, 0, 0, 4}, {{.offset = 0, .symbol = "elsewhere"}}));
    return objects;
}

} // namespace

TEST_CASE("a symbol's bytes come back as they were before addresses were patched in",
          "[z64][image]") {
    const z64::MemoryImage image = z64::layOutImage(
        makeObjects(), z64::LayoutOptions{.findExternal = [](const std::string_view name) {
            return name == "elsewhere" ? std::optional<base::u32>(0x0200'0100) : std::nullopt;
        }});
    REQUIRE((*image.findBytes(0x0300'0004, 8))[3] == 0x00);
    REQUIRE((*image.findBytes(0x0300'0008, 4))[3] == 0x08);

    const std::vector<z64::SymbolData> symbols = z64::listSymbolData(image, 3);

    REQUIRE(symbols.size() == 3);
    REQUIRE(symbols[1].name == "list");
    REQUIRE(symbols[1].offset == 4);
    REQUIRE(symbols[1].bytes == std::vector<u8>{0, 0, 0, 0, 0, 0, 0, 8});
    REQUIRE(symbols[1].relocations.size() == 2);
    REQUIRE(symbols[1].relocations[1].offset == 4);
    REQUIRE(symbols[1].relocations[1].symbol == "first");
    REQUIRE(symbols[2].bytes == std::vector<u8>{0, 0, 0, 4});
    REQUIRE(z64::findSymbolData(image, "outside")->relocations.front().symbol == "elsewhere");
    REQUIRE_FALSE(z64::findSymbolData(image, "nothing").has_value());
}

TEST_CASE("symbols laid out again make the image they came from", "[z64][image]") {
    const z64::MemoryImage image = z64::layOutImage(makeObjects(), {});

    std::vector<z64::ImageObject> objects;
    for (const z64::SymbolData& symbol : z64::listSymbolData(image, 3)) {
        objects.push_back(z64::makeImageObject(symbol));
    }
    const z64::MemoryImage again = z64::layOutImage(std::move(objects), {});

    REQUIRE(again.segments.front().bytes == image.segments.front().bytes);
    REQUIRE(z64::listSymbolData(again, 3) == z64::listSymbolData(image, 3));
}
