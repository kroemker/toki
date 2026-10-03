#include "n64/Segment.h"

#include "n64/Types.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("segment addresses round-trip", "[n64][segment]") {
    const n64::SegmentAddress address{.segment = 0x03, .offset = 0x0012'3456};

    const n64::u32 raw = n64::pack(address);

    REQUIRE(raw == 0x0312'3456);
    REQUIRE(n64::unpack(raw).segment == address.segment);
    REQUIRE(n64::unpack(raw).offset == address.offset);
}

TEST_CASE("the selector is masked to four bits", "[n64][segment]") {
    REQUIRE(n64::pack({.segment = 0xF3, .offset = 0}) == 0x0300'0000);
}

TEST_CASE("the offset is masked to twenty-four bits", "[n64][segment]") {
    REQUIRE(n64::pack({.segment = 0x02, .offset = 0xFF00'0001}) == 0x0200'0001);
}

TEST_CASE("segment zero means the pointer needs no resolving", "[n64][segment]") {
    REQUIRE_FALSE(n64::isSegmented(0x0000'1234));
    REQUIRE(n64::isSegmented(0x0200'1234));
}
