#include "cparser/Calls.h"

#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view Source = R"(
static Vec3f sPosition = { 0.0f, 0.0f, 0.0f };

void EnTest_Setup(EnTest* this, PlayState* play);

static void EnTest_SetupSkeleton(EnTest* this, PlayState* play) {
    Actor_SetScale(&this->actor, 0.01f);
}

void EnTest_Init(Actor* thisx, PlayState* play) {
    EnTest* this = (EnTest*)thisx;
#if 0
    SkelAnime_Init(play, &this->skelAnime, &gWrongSkel, NULL, NULL, NULL, 0);
#endif
    SkelAnime_InitFlex(play, &this->skelAnime, (FlexSkeletonHeader*)&gTestSkel, NULL,
                       this->jointTable, this->morphTable, getLimbCount(1, 2));
    this->skelAnime.SkelAnime_Init(play);
    EnTest_SetupSkeleton(this, play);
}

Gfx* __attribute__((unused)) EnTest_MakeList(void) {
    return NULL;
}
)";

[[nodiscard]] std::string spell(const std::span<const cparser::Token> tokens) {
    std::string spelled;
    for (const cparser::Token& token : tokens) {
        spelled += token.text;
    }
    return spelled;
}

struct Parsed {
    cparser::MacroTable base;
    cparser::TranslationUnit unit;
    cparser::ParseResult result;

    Parsed()
        : unit(cparser::preprocessText(base, "en_test.c", std::string(Source), {})),
          result(cparser::parseDeclarations(unit, cparser::EvalContext{})) {}

    [[nodiscard]] const cparser::FunctionDefinition& find(const std::string_view name) const {
        const auto found =
            std::ranges::find(result.functions, name, &cparser::FunctionDefinition::name);
        REQUIRE(found != result.functions.end());
        return *found;
    }
};

} // namespace

TEST_CASE("function definitions are found with their bodies, prototypes are not",
          "[cparser][calls]") {
    const Parsed parsed;

    REQUIRE(parsed.result.functions.size() == 3);
    REQUIRE(parsed.find("EnTest_SetupSkeleton").isStatic);
    REQUIRE_FALSE(parsed.find("EnTest_Init").isStatic);
    REQUIRE(spell(parsed.find("EnTest_MakeList").body) == "returnNULL;");
    REQUIRE(parsed.find("EnTest_Init").location.line == 10);
    REQUIRE(parsed.result.declarations.size() == 1);
    REQUIRE(parsed.result.declarations.front().name == "sPosition");
}

TEST_CASE("calls are found in a body with their arguments, nested ones too", "[cparser][calls]") {
    const Parsed parsed;
    const std::vector<std::string_view> wanted{"SkelAnime_Init", "SkelAnime_InitFlex",
                                               "getLimbCount", "EnTest_SetupSkeleton"};

    const std::vector<cparser::Call> calls =
        cparser::findCalls(parsed.find("EnTest_Init").body, [&wanted](const std::string_view name) {
            return std::ranges::find(wanted, name) != wanted.end();
        });

    REQUIRE(calls.size() == 3);
    REQUIRE(calls[0].callee == "SkelAnime_InitFlex");
    REQUIRE(calls[0].arguments.size() == 7);
    REQUIRE(spell(calls[0].arguments[2]) == "(FlexSkeletonHeader*)&gTestSkel");
    REQUIRE(spell(calls[0].arguments[3]) == "NULL");
    REQUIRE(calls[0].location.line == 15);
    REQUIRE(calls[1].callee == "getLimbCount");
    REQUIRE(calls[1].arguments.size() == 2);
    REQUIRE(calls[2].callee == "EnTest_SetupSkeleton");
}

TEST_CASE("a call without arguments has none", "[cparser][calls]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, "draw.c", "void f(void) { g(); }\n", {});
    const cparser::ParseResult parsed = cparser::parseDeclarations(unit, cparser::EvalContext{});
    REQUIRE(parsed.functions.size() == 1);

    const std::vector<cparser::Call> calls = cparser::findCalls(
        parsed.functions.front().body, [](const std::string_view name) { return name == "g"; });

    REQUIRE(calls.size() == 1);
    REQUIRE(calls.front().arguments.empty());
}
