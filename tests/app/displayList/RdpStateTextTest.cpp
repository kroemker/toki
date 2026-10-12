#include "app/displayList/domain/RdpStateText.h"

#include "n64/CombineMode.h"
#include "n64/Rdp.h"
#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

TEST_CASE("the RDP state reads as its parts, the combiner as equations", "[displayList]") {
    n64::RdpState state;
    state.combineMode = n64::makeCombineMode(0xFCFF'FFFF, 0xFFFE'793C);
    state.primitive = {255, 128, 0, 255};
    state.tiles[0] =
        n64::TileDescriptor{.format = n64::TextureFormat::Rgba,
                            .size = n64::TexelSize::Bits16,
                            .line = 2,
                            .axes = {n64::TileAxis{.high = 28}, n64::TileAxis{.high = 12}}};

    const std::vector<toki::displayList::StateSection> sections =
        toki::displayList::describeRdpState(state);

    REQUIRE(sections.size() == 5);
    const toki::displayList::StateSection& combiner = sections[0];
    REQUIRE(combiner.lines.size() == 2);
    REQUIRE(combiner.lines[0].label == "cycle 2 colour");
    REQUIRE(combiner.lines[0].value == "(0 − 0) × 0 + SHADE  =  SHADE");
    REQUIRE(combiner.lines[1].value == "(0 − 0) × 0 + SHADE  =  SHADE");
    REQUIRE(sections[3].lines[0].value == "255, 128, 0, 255");
    REQUIRE(sections[4].lines.size() == 1);
    REQUIRE(sections[4].lines[0].value.starts_with("RGBA16 at 0x000, rows of 2 words"));

    const std::optional<toki::displayList::TilePreview> preview =
        toki::displayList::makeTilePreview(state, n64::TextureMemoryImage{}, 0);
    REQUIRE(preview.has_value());
    REQUIRE(preview->width == 8);
    REQUIRE(preview->height == 4);
    REQUIRE(preview->pixels.size() == 8 * 4 * 4);
    REQUIRE_FALSE(toki::displayList::makeTilePreview(state, n64::TextureMemoryImage{}, 1));
    REQUIRE_FALSE(toki::displayList::findGlossaryEntry("texture memory").empty());
}
