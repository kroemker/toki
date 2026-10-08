#include "z64/PngImage.h"

#include "base/Types.h"
#include "n64/Color.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <vector>

using base::u8;

TEST_CASE("an RGBA image encoded as a PNG decodes to the same pixels", "[z64][png]") {
    const std::vector<u8> rgba{0xFF, 0x00, 0x00, 0xFF, 0x00, 0x80, 0xFF, 0x40};

    const std::expected<std::vector<u8>, std::string> encoded = z64::encodePng(rgba, 2, 1);
    REQUIRE(encoded.has_value());
    const std::expected<z64::PngImage, std::string> decoded = z64::decodePng(*encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->width == 2);
    REQUIRE(decoded->texels ==
            std::vector<n64::Color>{{0xFF, 0x00, 0x00, 0xFF}, {0x00, 0x80, 0xFF, 0x40}});
}
