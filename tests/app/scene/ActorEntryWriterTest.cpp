#include "app/scene/domain/ActorEntryWriter.h"

#include "support/WritingHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {

constexpr std::string_view Header =
    "typedef signed char s8;\n"
    "typedef short s16;\n"
    "typedef struct { s16 x, y, z; } Vec3s;\n"
    "typedef struct ActorEntry { s16 id; Vec3s pos; Vec3s rot; s16 params; } ActorEntry;\n"
    "typedef struct TransitionActorEntry { struct { s8 room; s8 bgCamIndex; } sides[2]; "
    "s16 id; Vec3s pos; s16 rotY; s16 params; } TransitionActorEntry;\n"
    "typedef enum ActorID { ACTOR_PLAYER, ACTOR_EN_TEST, ACTOR_DOOR_ANA } ActorID;\n";

class Harness : public testing::WritingHarness {
public:
    Harness() : WritingHarness(Header, {}) {}

    std::string writeEntries(const std::string& source) {
        return writeAgain(source,
                          makeWriter({{"ActorEntry", toki::scene::makeActorEntryWriter(getTypes())},
                                      {"TransitionActorEntry",
                                       toki::scene::makeTransitionActorEntryWriter(getTypes())}}));
    }
};

} // namespace

TEST_CASE("a placed actor is written by its name, its rotation and params in hex",
          "[scene][writer]") {
    Harness harness;

    REQUIRE(harness.writeEntries("ActorEntry actors[] = {\n"
                                 "    { ACTOR_EN_TEST, { 398, -29, -483 }, { 0, 0x8000, 0xB6 }, "
                                 "0xFFFF },\n"
                                 "    { 7, { 0, 0, 0 }, { 0, 0, 0 }, 1 },\n"
                                 "};\n") ==
            "ActorEntry actors[2] = {\n"
            "    { ACTOR_EN_TEST, { 398, -29, -483 }, { 0x0000, 0x8000, 0x00B6 }, 0xFFFF },\n"
            "    { 7, { 0, 0, 0 }, { 0x0000, 0x0000, 0x0000 }, 0x0001 },\n"
            "};\n");
}

TEST_CASE("a transition actor is written with its sides first", "[scene][writer]") {
    Harness harness;

    REQUIRE(harness.writeEntries("TransitionActorEntry doors[] = {\n"
                                 "    { { { 0, -1 }, { 2, -1 } }, ACTOR_DOOR_ANA, { -785, 120, "
                                 "1219 }, 0xC000, 0x003F },\n"
                                 "};\n") ==
            "TransitionActorEntry doors[1] = {\n"
            "    { { { 0, -1 }, { 2, -1 } }, ACTOR_DOOR_ANA, { -785, 120, 1219 }, 0xC000, "
            "0x003F },\n"
            "};\n");
}
