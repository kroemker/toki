#include "cparser/Expression.h"

#include "base/Diagnostic.h"
#include "cparser/Scanner.h"
#include "cparser/Scrape.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

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
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "cparser/fixtures";
}

/// An expression scanned as a file of its own. The tokens view into `file`,
/// so it is built in place and never copied.
class Expression {
public:
    explicit Expression(std::string text)
        : file{.path = "expression.c", .contents = std::move(text)}, result(cparser::scan(file)) {}

    [[nodiscard]] std::span<const cparser::Token> tokens() const {
        return std::span(result.tokens).first(result.tokens.size() - 1);
    }

private:
    cparser::SourceFile file;
    cparser::ScanResult result;
};

struct Harness {
    Harness() : scraped(cparser::scrapeDefinitions(fixtures() / "asset")) {
        REQUIRE(scraped.definitions > 0);
    }

    [[nodiscard]] cparser::EvalContext context() {
        return cparser::EvalContext{
            .macros = &scraped.macros,
            .arrayLength = [](std::string_view name) -> std::optional<cparser::usize> {
                return name == "sVertices" ? std::optional<cparser::usize>(52) : std::nullopt;
            },
        };
    }

    [[nodiscard]] cparser::Value value(std::string text) {
        const Expression expression(std::move(text));
        const std::expected<cparser::Value, base::Diagnostic> evaluated =
            cparser::evaluate(expression.tokens(), context(), diagnostics);
        REQUIRE(evaluated.has_value());
        return *evaluated;
    }

    [[nodiscard]] cparser::s64 integer(std::string text) {
        return cparser::asInteger(value(std::move(text)));
    }

    std::vector<base::Diagnostic> diagnostics;
    cparser::ScrapeResult scraped;
};

} // namespace

TEST_CASE("integer literals carry their base and suffix", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("0x0A") == 10);
    REQUIRE(harness.integer("010") == 8);
    REQUIRE(harness.integer("42u") == 42);
    REQUIRE(harness.integer("0xFFFFFFFFuL") == 0xFFFF'FFFF);
    REQUIRE(harness.integer("'A'") == 65);
    REQUIRE(harness.integer("'\\0'") == 0);
}

TEST_CASE("operators follow C precedence", "[cparser][expression]") {
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

TEST_CASE("a cast truncates the way the decomp's type does", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(s16)(0x8000 | 100)") == -32668);
    REQUIRE(harness.integer("(u8)0x1FF") == 0xFF);
    REQUIRE(harness.integer("(s16)((((1) / 4) << 10) | (970))") == 970);
}

TEST_CASE("macros are expanded before evaluation", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("LENGTH_sActorList") == 2);
    REQUIRE(harness.integer("TRUNCF_BINANG(3.9)") == 3);
}

TEST_CASE("a rotation written as degrees survives the macro chain", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("DEG_TO_BINANG(0.000)") == 0);

    REQUIRE(harness.integer("DEG_TO_BINANG(90.0)") == 16384);
}

TEST_CASE("a rotation lands on the binary angle C computes, float rounding included",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("DEG_TO_BINANG(210.13)") == -27283);
    REQUIRE(harness.integer("DEG_TO_BINANG(300.13)") == -10899);
}

TEST_CASE("float arithmetic stays in single precision", "[cparser][expression]") {
    Harness harness;

    const cparser::Value literal = harness.value("180.0f");
    REQUIRE(literal.type == cparser::ValueType::Float);

    const cparser::Value quotient = harness.value("0x8000 / 180.0f");
    REQUIRE(quotient.type == cparser::ValueType::Float);
    REQUIRE(quotient.number == static_cast<cparser::f64>(32768.0F / 180.0F));

    const cparser::Value third = harness.value("1 / 3.0f");
    REQUIRE(third.number == static_cast<cparser::f64>(1.0F / 3.0F));

    REQUIRE(harness.value("-1.5f").type == cparser::ValueType::Float);
}

TEST_CASE("an integer meeting a float is converted to float before the operation",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.value("16777217 - 16777216.0f").number == 0.0);
    REQUIRE(harness.value("16777217 - 16777216.0").number == 1.0);
}

TEST_CASE("a float past the largest one rounds the way the hardware does",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.value("(f32)3.4028235e38").number ==
            static_cast<cparser::f64>(std::numeric_limits<cparser::f32>::max()));
    REQUIRE(harness.value("(f32)-3.4028235e38").number ==
            -static_cast<cparser::f64>(std::numeric_limits<cparser::f32>::max()));
    REQUIRE(std::isinf(harness.value("(f32)3.5e38").number));
}

TEST_CASE("a double anywhere makes the whole operation double", "[cparser][expression]") {
    Harness harness;

    const cparser::Value mixed = harness.value("0.1f * 0.1");
    REQUIRE(mixed.type == cparser::ValueType::Double);
    REQUIRE(mixed.number == static_cast<cparser::f64>(0.1F) * 0.1);

    REQUIRE(harness.value("0.1 + 0.2").number == 0.1 + 0.2);
}

TEST_CASE("a float cast rounds and a double cast widens", "[cparser][expression]") {
    Harness harness;

    const cparser::Value narrowed = harness.value("(f32)0.1");
    REQUIRE(narrowed.type == cparser::ValueType::Float);
    REQUIRE(narrowed.number == static_cast<cparser::f64>(0.1F));

    const cparser::Value widened = harness.value("(f64)0.1f");
    REQUIRE(widened.type == cparser::ValueType::Double);
    REQUIRE(widened.number == static_cast<cparser::f64>(0.1F));
}

TEST_CASE("ARRAY_COUNT and TEX_LEN are answered by the parser, not by sizeof",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("ARRAY_COUNT(sVertices)") == 52);
    REQUIRE(harness.integer("TEX_LEN(u64, 32, 32, 16)") == 256);
    REQUIRE(harness.integer("TEX_LEN(u8, 16, 16, 4)") == 128);
}

TEST_CASE("an identifier nothing defines is a zero and a diagnostic", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("ACTOR_PLAYER") == 0);
    REQUIRE(harness.diagnostics.size() == 1);
    REQUIRE(harness.diagnostics.front().message.contains("ACTOR_PLAYER"));
}

TEST_CASE("a resolver answers the identifiers the tables generate", "[cparser][expression]") {
    Harness harness;
    cparser::EvalContext context = harness.context();
    context.resolve = [](std::string_view name) -> std::optional<cparser::Value> {
        return name == "ACTOR_PLAYER" ? std::optional<cparser::Value>(cparser::integerValue(0x0009))
                                      : std::nullopt;
    };

    const Expression expression("ACTOR_PLAYER | 0x100");
    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(expression.tokens(), context, harness.diagnostics);

    REQUIRE(value.has_value());
    REQUIRE(cparser::asInteger(*value) == 0x109);
    REQUIRE(harness.diagnostics.empty());
}

TEST_CASE("a kept name is never expanded", "[cparser][expression]") {
    Harness harness;
    cparser::EvalContext context = harness.context();
    context.keep = [](std::string_view name) { return name == "LENGTH_sActorList"; };

    const Expression expression("LENGTH_sActorList");
    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(expression.tokens(), context, harness.diagnostics);

    REQUIRE(value.has_value());
    REQUIRE(cparser::asInteger(*value) == 0);
    REQUIRE(harness.diagnostics.size() == 1);
}

TEST_CASE("a malformed expression is an error, not a zero", "[cparser][expression]") {
    Harness harness;
    const Expression expression("1 +");

    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(expression.tokens(), harness.context(), harness.diagnostics);

    REQUIRE_FALSE(value.has_value());
    REQUIRE(value.error().severity == base::Severity::Error);
}

TEST_CASE("an expression that ends early says where it ended", "[cparser][expression]") {
    Harness harness;
    const Expression expression("1 +");

    const std::expected<cparser::Value, base::Diagnostic> value =
        cparser::evaluate(expression.tokens(), harness.context(), harness.diagnostics);

    REQUIRE_FALSE(value.has_value());
    REQUIRE(value.error().location.file == "expression.c");
    REQUIRE(value.error().location.line == 1);
    REQUIRE(value.error().location.column == 4);
}

TEST_CASE("a real is true when it is nonzero", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("1.0 && 1") == 1);
    REQUIRE(harness.integer("0.5 && 1") == 1);
    REQUIRE(harness.integer("0.0 || 0") == 0);
    REQUIRE(harness.integer("!0.5") == 0);
    REQUIRE(harness.integer("0.5 ? 2 : 3") == 2);
}

TEST_CASE("the side a condition already decided is not evaluated", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("0 && UNKNOWN_NAME") == 0);
    REQUIRE(harness.integer("1 || UNKNOWN_NAME") == 1);
    REQUIRE(harness.integer("1 ? 2 : UNKNOWN_NAME") == 2);
    REQUIRE(harness.integer("0 && 1 / 0") == 0);
    REQUIRE(harness.diagnostics.empty());

    REQUIRE(harness.integer("1 && UNKNOWN_NAME") == 0);
    REQUIRE(harness.diagnostics.size() == 1);
}

TEST_CASE("integer division by zero is reported, real division is IEEE", "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("1 / 0") == 0);
    REQUIRE(harness.integer("5 % 0") == 0);
    REQUIRE(harness.diagnostics.size() == 2);
    REQUIRE(harness.diagnostics.front().severity == base::Severity::Warning);
    REQUIRE(harness.diagnostics.front().message.contains("by zero"));

    REQUIRE(std::isinf(harness.value("1.0 / 0").number));
    REQUIRE(std::isnan(harness.value("0.0 / 0").number));
    REQUIRE(harness.diagnostics.size() == 2);
}

TEST_CASE("the most negative integer divided by -1 wraps instead of trapping",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(-0x7FFFFFFFFFFFFFFF - 1) / -1") ==
            std::numeric_limits<cparser::s64>::min());
    REQUIRE(harness.integer("(-0x7FFFFFFFFFFFFFFF - 1) % -1") == 0);
}

TEST_CASE("a real too large for an integer saturates instead of being undefined",
          "[cparser][expression]") {
    REQUIRE(cparser::asInteger(cparser::doubleValue(1.0e300)) ==
            std::numeric_limits<cparser::s64>::max());
    REQUIRE(cparser::asInteger(cparser::doubleValue(-1.0e300)) ==
            std::numeric_limits<cparser::s64>::min());
    REQUIRE(cparser::asInteger(cparser::doubleValue(std::numeric_limits<cparser::f64>::quiet_NaN())) ==
            0);
}

TEST_CASE("char is unsigned and long is 32 bits, as the N64 build has them",
          "[cparser][expression]") {
    Harness harness;

    REQUIRE(harness.integer("(char)-1") == 255);
    REQUIRE(harness.integer("(s8)-1") == -1);
    REQUIRE(harness.integer("(long)0x100000001") == 1);
}
