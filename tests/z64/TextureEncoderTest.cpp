#include "z64/TextureEncoder.h"

#include "base/Types.h"
#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "z64/TextureIncludes.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using base::u8;

namespace {

std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

std::expected<std::vector<u8>, std::string>
encodeInclude(const std::filesystem::path& candidate,
              const std::vector<std::filesystem::path>& assetRoots = {}) {
    cparser::DirectoryCache directories;
    const std::optional<cparser::ResolvedInclude> resolved =
        z64::resolveTextureInclude(candidate, directories);
    REQUIRE(resolved.has_value());
    z64::TextureEncoder encoder(directories, assetRoots);
    return encoder.encode(resolved->path, resolved->tag);
}

std::expected<std::vector<u8>, std::string> encodeTexture(const char* spelling) {
    return encodeInclude(getFixtures() / "textures" / spelling);
}

std::vector<u8> requireEncoded(const char* spelling) {
    const std::expected<std::vector<u8>, std::string> bytes = encodeTexture(spelling);
    INFO((bytes ? std::string() : bytes.error()));
    REQUIRE(bytes.has_value());
    return *bytes;
}

} // namespace

TEST_CASE("an image packs into its format, padded to its element type", "[z64][texture-encoder]") {
    REQUIRE(requireEncoded("sCheck.rgba16.inc.c") ==
            std::vector<u8>{0xF8, 0x01, 0x08, 0x86, 0, 0, 0, 0});
    REQUIRE(requireEncoded("sGlow.ia8.inc.c") == std::vector<u8>{0xFF, 0x08, 0, 0, 0, 0, 0, 0});
    REQUIRE(requireEncoded("sGrey.i4.inc.c") == std::vector<u8>{0xF8, 0, 0, 0, 0, 0, 0, 0});
}

TEST_CASE("an encoded image's size is kept", "[z64][texture-encoder]") {
    cparser::DirectoryCache directories;
    z64::TextureEncoder encoder(directories);
    const std::filesystem::path image = getFixtures() / "textures/sCheck.rgba16.png";

    REQUIRE_FALSE(encoder.findImageSize(image).has_value());
    REQUIRE(encoder.encode(image, z64::ImageTag).has_value());
    REQUIRE(encoder.findImageSize(image) == std::optional(std::pair<base::u32, base::u32>(2, 1)));
}

TEST_CASE("a colour-indexed image keeps the PNG's own indices", "[z64][texture-encoder]") {
    REQUIRE(requireEncoded("sCrate.ci4.inc.c") == std::vector<u8>{0x01, 0x20, 0, 0, 0, 0, 0, 0});
    REQUIRE(requireEncoded("sWall.ci8.tlut_sShared.inc.c") ==
            std::vector<u8>{1, 0, 0, 0, 0, 0, 0, 0});
}

TEST_CASE("a palette is the PNG's, transparency included, padded to eight bytes",
          "[z64][texture-encoder]") {
    REQUIRE(requireEncoded("sCrate.tlut.rgba16.inc.c") ==
            std::vector<u8>{0xF8, 0x01, 0x07, 0xC1, 0x00, 0x3E, 0x00, 0x00});
    REQUIRE(requireEncoded("sShared.tlut.rgba16.inc.c") ==
            std::vector<u8>{0xF8, 0x01, 0x07, 0xC1, 0x00, 0x00, 0x00, 0x00});
}

TEST_CASE("the high half of a split palette moves its indices up by 128",
          "[z64][texture-encoder]") {
    REQUIRE(requireEncoded("sSky.ci8.split_hi.tlut_sSkyHigh.inc.c") ==
            std::vector<u8>{128, 129, 0, 0, 0, 0, 0, 0});

    const std::vector<u8> palette = requireEncoded("sSkyHigh.tlut.rgba16.inc.c");
    REQUIRE(palette.size() == 256);
    REQUIRE(palette[0] == 0xF8);
    REQUIRE(palette[255] == 0x00);
}

TEST_CASE("a JPEG fills the background buffer and a binary is as it is", "[z64][texture-encoder]") {
    const std::vector<u8> jpeg = requireEncoded("sBackground.jpg.inc.c");
    REQUIRE(jpeg.size() == base::usize{320} * 240 * 2);
    REQUIRE(std::vector<u8>(jpeg.begin(), jpeg.begin() + 7) ==
            std::vector<u8>{0xFF, 0xD8, 'J', 'F', 'I', 'F', 0});

    REQUIRE(requireEncoded("sData.bin.inc.c").size() == 19);
}

TEST_CASE("what the build would quantize is refused with a reason", "[z64][texture-encoder]") {
    const std::expected<std::vector<u8>, std::string> truecolor = encodeTexture("sBad.ci8.inc.c");
    REQUIRE_FALSE(truecolor.has_value());
    REQUIRE(truecolor.error().contains("sBad.ci8.png"));

    const std::expected<std::vector<u8>, std::string> mixed =
        encodeTexture("sMixed.tlut.rgba16.inc.c");
    REQUIRE_FALSE(mixed.has_value());
    REQUIRE(mixed.error().contains("sMixed"));
}

TEST_CASE("images sharing a palette must agree on how they use it", "[z64][texture-encoder]") {
    const std::expected<std::vector<u8>, std::string> odd = encodeTexture("sOdd.tlut.rgba16.inc.c");

    REQUIRE_FALSE(odd.has_value());
    REQUIRE(odd.error().contains("sHalfB"));
}

TEST_CASE("a shared palette is gathered across overlaid asset roots, the first winning",
          "[z64][texture-encoder]") {
    const std::filesystem::path overlay = getFixtures() / "overlay";
    const std::vector<std::filesystem::path> roots{overlay / "assets", overlay / "extracted"};
    const std::filesystem::path candidate =
        overlay / "extracted/objects/object_o/sOther.tlut.rgba16.inc.c";

    const std::expected<std::vector<u8>, std::string> overlaid = encodeInclude(candidate, roots);
    INFO((overlaid ? std::string() : overlaid.error()));
    REQUIRE(overlaid.has_value());
    REQUIRE(*overlaid == std::vector<u8>{0x00, 0x3F, 0, 0, 0, 0, 0, 0});

    REQUIRE_FALSE(encodeInclude(candidate).has_value());
}
