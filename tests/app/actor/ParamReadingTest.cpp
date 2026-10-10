#include "app/actor/domain/ParamReading.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view Example = R"(format = 2

[actors.ACTOR_OBJ_SYOKUDAI.params]
"Switch Flag" = { mask = 0x003F, kind = "number", description = "Set when it is lit" }
"Lit"         = { mask = 0x0400, kind = "flag" }
"Type"        = { mask = 0xF000, kind = "choice", values = { 0 = "Golden", 1 = "Timed" } }
"Text"        = { mask = 0xFFFF, source = "rotation_x", kind = "number", hex = true }

[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
match = { "Type" = 1, age = "adult" }

[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
)";

[[nodiscard]] toki::actor::ActorEntry readTorch() {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(Example, "a.toml");
    REQUIRE(database.diagnostics.empty());
    return database.entries.at("ACTOR_OBJ_SYOKUDAI");
}

} // namespace

TEST_CASE("a placement's params are read as the panels show them", "[actor][params]") {
    const toki::actor::ActorEntry torch = readTorch();

    const std::vector<toki::actor::ParamReading> params =
        toki::actor::readParams(torch, {.params = 0x1405, .rotation = {0x0300, 0, 0}}, {});

    REQUIRE(params.size() == 4);
    REQUIRE(params[0].name == "Switch Flag");
    REQUIRE(params[0].text == "5");
    REQUIRE(params[0].description == "Set when it is lit");
    REQUIRE(params[1].text == "yes");
    REQUIRE(params[2].text == "Timed");
    REQUIRE(params[3].text == "0x300");
    REQUIRE(toki::actor::readParams(torch, {.params = 0x7000}, {})[2].text == "7 (no label)");
}

TEST_CASE("the match for a placement gives every param's value", "[actor][params]") {
    const toki::actor::ActorEntry torch = readTorch();

    const std::vector<toki::actor::ParamMatch> match =
        toki::actor::makeParamsMatch(torch, {.params = 0x1005});

    const auto findValue = [&match](const std::string_view param) {
        const auto found = std::ranges::find(match, param, &toki::actor::ParamMatch::param);
        REQUIRE(found != match.end());
        REQUIRE(found->values.size() == 1);
        return found->values.front();
    };
    REQUIRE(match.size() == 4);
    REQUIRE(std::get<base::s64>(findValue("Switch Flag")) == 5);
    REQUIRE_FALSE(std::get<bool>(findValue("Lit")));
    REQUIRE(std::get<base::s64>(findValue("Type")) == 1);
}

TEST_CASE("a signed hexadecimal param is shown with its sign before the prefix",
          "[actor][params]") {
    const toki::actor::ActorParam param{
        .name = "Offset", .mask = 0x00FF, .isSigned = true, .isHex = true};

    REQUIRE(toki::actor::makeParamText(param, -5, nullptr) == "-0x5");
    REQUIRE(toki::actor::makeParamText(param, 0x1F, nullptr) == "0x1F");
}

TEST_CASE("a choice of a collection is shown by its constant's name, after its offset",
          "[actor][params]") {
    const toki::actor::ActorParam param{.name = "Text",
                                        .kind = toki::actor::ParamKind::Choice,
                                        .mask = 0x00FF,
                                        .isHex = true,
                                        .collection = "MessageID",
                                        .offset = 0x300};
    const toki::actor::ValueCollection messages{.name = "MessageID",
                                                .values = {{.value = 0x301, .label = "MSG_SIGN"}}};

    REQUIRE(toki::actor::makeParamText(param, 1, &messages) == "MSG_SIGN");
    REQUIRE(toki::actor::makeParamText(param, 2, &messages) == "0x302 (not in MessageID)");
    REQUIRE(toki::actor::makeParamText(param, 1, nullptr) == "0x301");
    REQUIRE(toki::actor::findParamCollection(
                param, [&messages](std::string_view name) -> const toki::actor::ValueCollection* {
                    return name == messages.name ? &messages : nullptr;
                }) == &messages);
    REQUIRE(toki::actor::findParamCollection(param, {}) == nullptr);
}
