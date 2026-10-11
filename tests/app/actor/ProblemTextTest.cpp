#include "app/actor/domain/ProblemText.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a problem's path into the entry is told as words", "[actor][names]") {
    const toki::actor::ProblemText part = toki::actor::makeProblemText(
        "ACTOR_EN_BOX", "ACTOR_EN_BOX.variants[0].parts[1]: draws nothing: it names no skeleton");
    REQUIRE(part.place == "Variant 1 › Part 2");
    REQUIRE(part.message == "Draws nothing: it names no skeleton");

    const toki::actor::ProblemText match = toki::actor::makeProblemText(
        "ACTOR_EN_BOX", "ACTOR_EN_BOX.variants[2].match.Item: 1 is not one of GetItemID");
    REQUIRE(match.place == "Variant 3 › Condition on \"Item\"");

    const toki::actor::ProblemText param = toki::actor::makeProblemText(
        "ACTOR_EN_BOX", "ACTOR_EN_BOX.params.Switch Flag: has no bits");
    REQUIRE(param.place == "Param \"Switch Flag\"");
    REQUIRE(param.message == "Has no bits");

    const toki::actor::ProblemText plain =
        toki::actor::makeProblemText("ACTOR_EN_BOX", "the file could not be read");
    REQUIRE(plain.place.empty());
    REQUIRE(plain.message == "The file could not be read");
}
