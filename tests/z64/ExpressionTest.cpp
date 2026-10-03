#include "z64/Expression.h"

#include "z64/Diagnostic.h"
#include "z64/Scanner.h"
#include "z64/Scrape.h"
#include "z64/SourceFile.h"
#include "z64/Token.h"
#include "z64/Types.h"
#include "z64/Value.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

/// An expression scanned as a file of its own. The tokens view into `file`,
/// so it is built in place and never copied.
class Expression {
public:
    explicit Expression(std::string text)
        : file{.path = "expression.c", .contents = std::move(text)}, result(z64::scan(file)) {}

    [[nodiscard]] std::span<const z64::Token> tokens() const {
        return std::span(result.tokens).first(result.tokens.size() - 1);
    }

private:
    z64::SourceFile file;
    z64::ScanResult result;
};

struct Harness {
    Harness() : scraped(z64::scrapeDefinitions(fixtures() / "asset")) {
        REQUIRE(scraped.definitions > 0);
    }

    [[nodiscard]] z64::EvalContext context() {
        return z64::EvalContext{
            .macros = &scraped.macros,
            .arrayLength = [](std::string_view name) -> std::optional<z64::usize> {
                return name == "sVertices" ? std::optional<z64::usize>(52) : std::nullopt;
            },
        };
    }

    [[nodiscard]] z64::Value value(std::string text) {
        const Expression expression(std::move(text));
        const std::expected<z64::Value, z64::Diagnostic> evaluated =
            z64::evaluate(expression.tokens(), context(), diagnostics);
        REQUIRE(evaluated.has_value());
        return *evaluated;
    }

    [[nodiscard]] z64::s64 integer(std::string text) {
        return z64::asInteger(value(std::move(text)));
    }

    std::vector<z64::Diagnostic> diagnostics;
    z64::ScrapeResult scraped;
};

} // namespace

TEST_CASE("integer literals carry their base and suffix", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("0x0A") == 10);
    REQUIRE(harness.integer("010") == 8);
    REQUIRE(harness.integer("42u") == 42);
    REQUIRE(harness.integer("0xFFFFFFFFuL") == 0xFFFF'FFFF);
    REQUIRE(harness.integer("'A'") == 65);
    REQUIRE(harness.integer("'\\0'") == 0);
}

TEST_CASE("operators follow C precedence", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("1 + 2 * 3") == 7);
    REQUIRE(harness.integer("(1 + 2) * 3") == 9);
    REQUIRE(harness.integer("1 << 3 | 0x80") == 0x88);
    REQUIRE(harness.integer("-1") == -1);
    REQUIRE(harness.integer("~0 & 0xFF") == 0xFF);
    REQUIRE(harness.integer("1 ? 2 : 3") == 2);
    REQUIRE(harness.integer("0 ? 2 : 3") == 3);
    REQUIRE(harness.integer("2 > 1 && 1 < 2") == 1);
}

TEST_CASE("a cast truncates the way the decomp's type does", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(s16)(0x8000 | 100)") == -32668);
    REQUIRE(harness.integer("(u8)0x1FF") == 0xFF);
    REQUIRE(harness.integer("(s16)((((1) / 4) << 10) | (970))") == 970);
}

TEST_CASE("macros are expanded before evaluation", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("LENGTH_sActorList") == 2);
    REQUIRE(harness.integer("TRUNCF_BINANG(3.9)") == 3);
}

TEST_CASE("a rotation written as degrees survives the macro chain", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("DEG_TO_BINANG(0.000)") == 0);

    REQUIRE(harness.integer("DEG_TO_BINANG(90.0)") == 16384);
}

TEST_CASE("a rotation lands on the binary angle C computes, float rounding included",
          "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("DEG_TO_BINANG(210.13)") == -27283);
    REQUIRE(harness.integer("DEG_TO_BINANG(300.13)") == -10899);
}

TEST_CASE("float arithmetic stays in single precision", "[z64][expression]") {
    Harness harness;

    const z64::Value literal = harness.value("180.0f");
    REQUIRE(literal.type == z64::ValueType::Float);

    const z64::Value quotient = harness.value("0x8000 / 180.0f");
    REQUIRE(quotient.type == z64::ValueType::Float);
    REQUIRE(quotient.number == static_cast<z64::f64>(32768.0F / 180.0F));

    const z64::Value third = harness.value("1 / 3.0f");
    REQUIRE(third.number == static_cast<z64::f64>(1.0F / 3.0F));

    REQUIRE(harness.value("-1.5f").type == z64::ValueType::Float);
}

TEST_CASE("an integer meeting a float is converted to float before the operation",
          "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.value("16777217 - 16777216.0f").number == 0.0);
    REQUIRE(harness.value("16777217 - 16777216.0").number == 1.0);
}

TEST_CASE("a float past the largest one rounds the way the hardware does", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.value("(f32)3.4028235e38").number ==
            static_cast<z64::f64>(std::numeric_limits<z64::f32>::max()));
    REQUIRE(harness.value("(f32)-3.4028235e38").number ==
            -static_cast<z64::f64>(std::numeric_limits<z64::f32>::max()));
    REQUIRE(std::isinf(harness.value("(f32)3.5e38").number));
}

TEST_CASE("a double anywhere makes the whole operation double", "[z64][expression]") {
    Harness harness;

    const z64::Value mixed = harness.value("0.1f * 0.1");
    REQUIRE(mixed.type == z64::ValueType::Double);
    REQUIRE(mixed.number == static_cast<z64::f64>(0.1F) * 0.1);

    REQUIRE(harness.value("0.1 + 0.2").number == 0.1 + 0.2);
}

TEST_CASE("a float cast rounds and a double cast widens", "[z64][expression]") {
    Harness harness;

    const z64::Value narrowed = harness.value("(f32)0.1");
    REQUIRE(narrowed.type == z64::ValueType::Float);
    REQUIRE(narrowed.number == static_cast<z64::f64>(0.1F));

    const z64::Value widened = harness.value("(f64)0.1f");
    REQUIRE(widened.type == z64::ValueType::Double);
    REQUIRE(widened.number == static_cast<z64::f64>(0.1F));
}

TEST_CASE("ARRAY_COUNT and TEX_LEN are answered by the parser, not by sizeof",
          "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("ARRAY_COUNT(sVertices)") == 52);
    REQUIRE(harness.integer("TEX_LEN(u64, 32, 32, 16)") == 256);
    REQUIRE(harness.integer("TEX_LEN(u8, 16, 16, 4)") == 128);
}

TEST_CASE("an identifier nothing defines is a zero and a diagnostic", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("ACTOR_PLAYER") == 0);
    REQUIRE(harness.diagnostics.size() == 1);
    REQUIRE(harness.diagnostics.front().message.contains("ACTOR_PLAYER"));
}

TEST_CASE("a resolver answers the identifiers the tables generate", "[z64][expression]") {
    Harness harness;
    z64::EvalContext context = harness.context();
    context.resolve = [](std::string_view name) -> std::optional<z64::Value> {
        return name == "ACTOR_PLAYER" ? std::optional<z64::Value>(z64::integerValue(0x0009))
                                      : std::nullopt;
    };

    const Expression expression("ACTOR_PLAYER | 0x100");
    const std::expected<z64::Value, z64::Diagnostic> value =
        z64::evaluate(expression.tokens(), context, harness.diagnostics);

    REQUIRE(value.has_value());
    REQUIRE(z64::asInteger(*value) == 0x109);
    REQUIRE(harness.diagnostics.empty());
}

TEST_CASE("a kept name is never expanded", "[z64][expression]") {
    Harness harness;
    z64::EvalContext context = harness.context();
    context.keep = [](std::string_view name) { return name == "LENGTH_sActorList"; };

    const Expression expression("LENGTH_sActorList");
    const std::expected<z64::Value, z64::Diagnostic> value =
        z64::evaluate(expression.tokens(), context, harness.diagnostics);

    REQUIRE(value.has_value());
    REQUIRE(z64::asInteger(*value) == 0);
    REQUIRE(harness.diagnostics.size() == 1);
}

TEST_CASE("a malformed expression is an error, not a zero", "[z64][expression]") {
    Harness harness;
    const Expression expression("1 +");

    const std::expected<z64::Value, z64::Diagnostic> value =
        z64::evaluate(expression.tokens(), harness.context(), harness.diagnostics);

    REQUIRE_FALSE(value.has_value());
    REQUIRE(value.error().severity == z64::Severity::Error);
}

TEST_CASE("an expression that ends early says where it ended", "[z64][expression]") {
    Harness harness;
    const Expression expression("1 +");

    const std::expected<z64::Value, z64::Diagnostic> value =
        z64::evaluate(expression.tokens(), harness.context(), harness.diagnostics);

    REQUIRE_FALSE(value.has_value());
    REQUIRE(value.error().location.file == "expression.c");
    REQUIRE(value.error().location.line == 1);
    REQUIRE(value.error().location.column == 4);
}

TEST_CASE("a real is true when it is nonzero", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("1.0 && 1") == 1);
    REQUIRE(harness.integer("0.5 && 1") == 1);
    REQUIRE(harness.integer("0.0 || 0") == 0);
    REQUIRE(harness.integer("!0.5") == 0);
    REQUIRE(harness.integer("0.5 ? 2 : 3") == 2);
}

TEST_CASE("the side a condition already decided is not evaluated", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("0 && UNKNOWN_NAME") == 0);
    REQUIRE(harness.integer("1 || UNKNOWN_NAME") == 1);
    REQUIRE(harness.integer("1 ? 2 : UNKNOWN_NAME") == 2);
    REQUIRE(harness.integer("0 && 1 / 0") == 0);
    REQUIRE(harness.diagnostics.empty());

    REQUIRE(harness.integer("1 && UNKNOWN_NAME") == 0);
    REQUIRE(harness.diagnostics.size() == 1);
}

TEST_CASE("integer division by zero is reported, real division is IEEE", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("1 / 0") == 0);
    REQUIRE(harness.integer("5 % 0") == 0);
    REQUIRE(harness.diagnostics.size() == 2);
    REQUIRE(harness.diagnostics.front().severity == z64::Severity::Warning);
    REQUIRE(harness.diagnostics.front().message.contains("by zero"));

    REQUIRE(std::isinf(harness.value("1.0 / 0").number));
    REQUIRE(std::isnan(harness.value("0.0 / 0").number));
    REQUIRE(harness.diagnostics.size() == 2);
}

TEST_CASE("the most negative integer divided by -1 wraps instead of trapping",
          "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(-0x7FFFFFFFFFFFFFFF - 1) / -1") ==
            std::numeric_limits<z64::s64>::min());
    REQUIRE(harness.integer("(-0x7FFFFFFFFFFFFFFF - 1) % -1") == 0);
}

TEST_CASE("a real too large for an integer saturates instead of being undefined",
          "[z64][expression]") {
    REQUIRE(z64::asInteger(z64::doubleValue(1.0e300)) == std::numeric_limits<z64::s64>::max());
    REQUIRE(z64::asInteger(z64::doubleValue(-1.0e300)) == std::numeric_limits<z64::s64>::min());
    REQUIRE(z64::asInteger(z64::doubleValue(std::numeric_limits<z64::f64>::quiet_NaN())) == 0);
}

TEST_CASE("char is unsigned and long is 32 bits, as the N64 build has them", "[z64][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(char)-1") == 255);
    REQUIRE(harness.integer("(s8)-1") == -1);
    REQUIRE(harness.integer("(long)0x100000001") == 1);
}
