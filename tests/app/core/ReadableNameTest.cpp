#include "app/core/domain/ReadableName.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("identifiers of every case read as words", "[core][names]") {
    REQUIRE(toki::core::makeReadableName("RiverSoundType") == "River Sound Type");
    REQUIRE(toki::core::makeReadableName("ActorID") == "Actor ID");
    REQUIRE(toki::core::makeReadableName("GetItemDrawID") == "Get Item Draw ID");
    REQUIRE(toki::core::makeReadableName("NA_BGM") == "NA BGM");
    REQUIRE(toki::core::makeReadableName("soundEffects") == "Sound Effects");
    REQUIRE(toki::core::makeReadableName("sGetItemIds") == "Get Item Ids");
    REQUIRE(toki::core::makeReadableName("switch_flag") == "Switch Flag");
    REQUIRE(toki::core::makeReadableName("Item00Type") == "Item00 Type");
    REQUIRE(toki::core::makeReadableName("D_80BA1B84") == "D 80BA1B84");
    REQUIRE(toki::core::makeReadableName("s") == "S");
}

TEST_CASE("constants lose the words they all start with and read as words", "[core][names]") {
    const std::vector<std::string> names{"GI_HEART_PIECE", "GI_BOMBS_5", "GI_DEKU_NUT_UPGRADE_30"};

    REQUIRE(toki::core::makeConstantLabels(names) ==
            std::vector<std::string>{"Heart Piece", "Bombs 5", "Deku Nut Upgrade 30"});
}

TEST_CASE("a shared prefix ends at a word and leaves every constant a word", "[core][names]") {
    REQUIRE(toki::core::makeConstantLabels(
                std::vector<std::string>{"SCENE_DEKU_TREE", "SCENE_DODONGOS_CAVERN"}) ==
            std::vector<std::string>{"Deku Tree", "Dodongos Cavern"});
    REQUIRE(toki::core::makeConstantLabels(
                std::vector<std::string>{"ENBOX_TYPE_BIG", "ENBOX_TYPE_BIG_ORNATE"}) ==
            std::vector<std::string>{"Big", "Big Ornate"});
    REQUIRE(toki::core::makeConstantLabels(std::vector<std::string>{"ENTR_DEKU_TREE_0"}) ==
            std::vector<std::string>{"Entr Deku Tree 0"});
}

TEST_CASE("abbreviations, words from a digit and names that are no constant keep their case",
          "[core][names]") {
    REQUIRE(toki::core::makeConstantLabels(std::vector<std::string>{
                "NA_BGM_FIELD_LOGIC", "NA_BGM_GENERAL_SFX", "NA_BGM_2D_SHOP"}) ==
            std::vector<std::string>{"Field Logic", "General SFX", "2D Shop"});
    REQUIRE(toki::core::makeConstantLabels(std::vector<std::string>{"GI_NONE", "5", "sTable"}) ==
            std::vector<std::string>{"Gi None", "5", "sTable"});
    REQUIRE(toki::core::makeConstantLabel("SCENE_HYRULE_FIELD", "SCENE_") == "Hyrule Field");
    REQUIRE(toki::core::makeConstantLabel("OBJECT_LINK_BOY", "SCENE_") == "Object Link Boy");
}
