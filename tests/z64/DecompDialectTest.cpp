#include "z64/DecompDialect.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "cparser/Value.h"
#include "z64/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view Rotations = "#define TRUNCF_BINANG(f) (s16)(s32)(f)\n"
                                       "#define DEG_TO_BINANG(degrees) "
                                       "(s16)TRUNCF_BINANG((degrees) * (0x8000 / 180.0f))\n";

/// Evaluates one expression in the decomp's dialect, after `definitions`, with
/// `sVertices` declared as 52 elements long.
struct Decomp {
    explicit Decomp(std::string_view definitions = {}) : definitions(definitions) {}

    [[nodiscard]] std::expected<cparser::Value, base::Diagnostic> evaluate(std::string expression) {
        units.push_back(cparser::preprocessText(
            base, "memory.c", definitions + "\n" + std::move(expression) + "\n", {}));
        const cparser::TranslationUnit& unit = units.back();
        const std::span<const cparser::Token> tokens =
            std::span(unit.tokens).first(unit.tokens.size() - 1);
        return cparser::evaluate(
            tokens,
            cparser::EvalContext{
                .macros = &unit.macros,
                .dialect = &z64::getDecompDialect(),
                .findArrayLength = [](std::string_view name) -> std::optional<z64::usize> {
                    return name == "sVertices" ? std::optional<z64::usize>(52) : std::nullopt;
                },
            },
            diagnostics);
    }

    [[nodiscard]] z64::s64 evaluateInteger(std::string expression) {
        const std::expected<cparser::Value, base::Diagnostic> value =
            evaluate(std::move(expression));
        REQUIRE(value.has_value());
        return cparser::getInteger(*value);
    }

    std::vector<base::Diagnostic> diagnostics;
    std::string definitions;
    cparser::MacroTable base;
    std::vector<cparser::TranslationUnit> units;
};

} // namespace

TEST_CASE("casts convert the way the N64 build's types do", "[z64][dialect]") {
    Decomp decomp;

    REQUIRE(decomp.evaluateInteger("(s16)(0x8000 | 100)") == -32668);
    REQUIRE(decomp.evaluateInteger("(u8)0x1FF") == 0xFF);
    REQUIRE(decomp.evaluateInteger("(char)-1") == 255);
    REQUIRE(decomp.evaluateInteger("(s8)-1") == -1);
    REQUIRE(decomp.evaluateInteger("(long)0x100000001") == 1);
    REQUIRE(decomp.evaluate("(f32)0.1")->type == cparser::ValueType::Float);
}

TEST_CASE("a rotation lands on the binary angle the build computes", "[z64][dialect]") {
    Decomp decomp(Rotations);

    REQUIRE(decomp.evaluateInteger("DEG_TO_BINANG(0.000)") == 0);
    REQUIRE(decomp.evaluateInteger("DEG_TO_BINANG(90.0)") == 16384);
    REQUIRE(decomp.evaluateInteger("DEG_TO_BINANG(210.13)") == -27283);
    REQUIRE(decomp.evaluateInteger("DEG_TO_BINANG(300.13)") == -10899);
    REQUIRE(decomp.evaluateInteger("TRUNCF_BINANG(3.9)") == 3);
}

TEST_CASE("TEX_LEN counts a texture's elements", "[z64][dialect]") {
    Decomp decomp;

    REQUIRE(decomp.evaluateInteger("TEX_LEN(u64, 32, 32, 16)") == 256);
    REQUIRE(decomp.evaluateInteger("TEX_LEN(u8, 16, 16, 4)") == 128);
    REQUIRE_FALSE(decomp.evaluate("TEX_LEN(Gfx, 16, 16, 4)").has_value());
    REQUIRE_FALSE(decomp.evaluate("TEX_LEN(u8, 16, 16)").has_value());
}

TEST_CASE("ARRAY_COUNT counts a declared array and warns about an undeclared one",
          "[z64][dialect]") {
    Decomp decomp;

    REQUIRE(decomp.evaluateInteger("ARRAY_COUNT(sVertices)") == 52);
    REQUIRE(decomp.diagnostics.empty());
    REQUIRE(decomp.evaluateInteger("ARRAY_COUNT(sNowhere)") == 0);
    REQUIRE(decomp.diagnostics.size() == 1);
    REQUIRE(decomp.diagnostics.front().severity == base::Severity::Warning);
}

TEST_CASE("a builtin is answered even where the decomp defines it with sizeof", "[z64][dialect]") {
    Decomp decomp("#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))");

    REQUIRE(decomp.evaluateInteger("ARRAY_COUNT(sVertices)") == 52);
}

TEST_CASE("an array bound written with TEX_LEN is evaluated", "[z64][dialect]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(base, "memory.c", "u64 sTex[TEX_LEN(u64, 32, 32, 16)];\n", {});
    const cparser::ParseResult parsed =
        cparser::parseDeclarations(unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().arrayLength == 256);
}
