#include "hackeroot/BuildMap.h"

#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

using base::u32;

namespace {

constexpr std::string_view MapText =
    R"( .bss           0x0000000080167dc0    0xad020 build/src/buffers/zbuffer.o
                0x0000000080167dc0                gZBuffer
                0x000000008018d5c0                gGfxPools
                0x0000000080214de0                _buffersSegmentEnd = .
                0x0000000000004b10                _bootSegmentBssSize = ABSOLUTE ((_bootSegmentBssEnd - _bootSegmentBssStart))
 *fill*         0x0000000080214de0       0x20
                0x0000000080999999                gZBuffer
)";

constexpr std::array Names = {std::string_view("gZBuffer"), std::string_view("gGfxPools"),
                              std::string_view("_buffersSegmentEnd"),
                              std::string_view("gAudioHeap")};

} // namespace

TEST_CASE("the map gives each symbol the address it lists first", "[hackeroot][map]") {
    const std::map<std::string, u32, std::less<>> symbols =
        hackeroot::findMapSymbols(MapText, Names);

    REQUIRE(symbols.size() == 3);
    REQUIRE(symbols.at("gZBuffer") == 0x80167DC0);
    REQUIRE(symbols.at("gGfxPools") == 0x8018D5C0);
    REQUIRE(symbols.at("_buffersSegmentEnd") == 0x80214DE0);
}

TEST_CASE("a map the build has not written is an error naming it", "[hackeroot][map]") {
    const std::filesystem::path missing =
        std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout/build/none.map";

    const std::expected<hackeroot::BuildMap, std::string> map =
        hackeroot::readBuildMap(missing, Names);

    REQUIRE_FALSE(map.has_value());
    REQUIRE(map.error().contains("none.map"));
}
