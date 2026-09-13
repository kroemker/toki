#include "z64/segment.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("segment addresses round-trip", "[z64][segment]") {
    const z64::SegmentAddr address{.segment = 0x03, .offset = 0x0012'3456};

    const z64::u32 raw = z64::pack(address);

    REQUIRE(raw == 0x0312'3456);
    REQUIRE(z64::unpack(raw).segment == address.segment);
    REQUIRE(z64::unpack(raw).offset == address.offset);
}

TEST_CASE("the selector is masked to four bits", "[z64][segment]") {
    REQUIRE(z64::pack({.segment = 0xF3, .offset = 0}) == 0x0300'0000);
}

TEST_CASE("the offset is masked to twenty-four bits", "[z64][segment]") {
    REQUIRE(z64::pack({.segment = 0x02, .offset = 0xFF00'0001}) == 0x0200'0001);
}

TEST_CASE("segment zero means the pointer needs no resolving", "[z64][segment]") {
    REQUIRE_FALSE(z64::is_segmented(0x0000'1234));
    REQUIRE(z64::is_segmented(0x0200'1234));
}
