#include "n64/Memory.h"

#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using base::u8;

TEST_CASE("bytes are found inside the region that holds them", "[n64][memory]") {
    const std::array<u8, 8> bytes{1, 2, 3, 4, 5, 6, 7, 8};
    n64::Memory memory;
    REQUIRE(memory.map(0x1000, bytes));

    REQUIRE(memory.findBytes(0x1002, 4)->front() == 3);
    REQUIRE(memory.findBytes(0x1000, 8).has_value());
    REQUIRE_FALSE(memory.findBytes(0x1004, 8).has_value());
    REQUIRE_FALSE(memory.findBytes(0x0FFF, 1).has_value());
}

TEST_CASE("a read never spans two regions", "[n64][memory]") {
    const std::array<u8, 4> first{};
    const std::array<u8, 4> second{};
    n64::Memory memory;
    REQUIRE(memory.map(0x2000, first));
    REQUIRE(memory.map(0x2004, second));

    REQUIRE(memory.findBytes(0x2004, 4).has_value());
    REQUIRE_FALSE(memory.findBytes(0x2002, 4).has_value());
}

TEST_CASE("overlapping regions are refused", "[n64][memory]") {
    const std::array<u8, 16> bytes{};
    n64::Memory memory;
    REQUIRE(memory.map(0x3000, bytes));

    REQUIRE_FALSE(memory.map(0x3008, bytes));
    REQUIRE_FALSE(memory.map(0x2FF8, bytes));
    REQUIRE(memory.map(0x3010, bytes));
}

TEST_CASE("a region past the 24-bit address space is refused", "[n64][memory]") {
    const std::array<u8, 16> bytes{};
    n64::Memory memory;

    REQUIRE_FALSE(memory.map(0x00FF'FFF8, bytes));
    REQUIRE(memory.map(0x00FF'FFF0, bytes));
}
