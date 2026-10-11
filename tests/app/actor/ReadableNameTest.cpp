#include "app/actor/domain/ReadableName.h"


#include <catch2/catch_test_macros.hpp>

TEST_CASE("identifiers of every case read as words", "[actor][names]") {
    REQUIRE(toki::actor::makeReadableName("RiverSoundType") == "River Sound Type");
    REQUIRE(toki::actor::makeReadableName("ActorID") == "Actor ID");
    REQUIRE(toki::actor::makeReadableName("GetItemDrawID") == "Get Item Draw ID");
    REQUIRE(toki::actor::makeReadableName("NA_BGM") == "NA BGM");
    REQUIRE(toki::actor::makeReadableName("soundEffects") == "Sound Effects");
    REQUIRE(toki::actor::makeReadableName("sGetItemIds") == "Get Item Ids");
    REQUIRE(toki::actor::makeReadableName("switch_flag") == "Switch Flag");
    REQUIRE(toki::actor::makeReadableName("Item00Type") == "Item00 Type");
    REQUIRE(toki::actor::makeReadableName("D_80BA1B84") == "D 80BA1B84");
    REQUIRE(toki::actor::makeReadableName("s") == "S");
}
