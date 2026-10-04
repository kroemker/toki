#include "base/Diagnostic.h"
#include "cparser/Dialect.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/ScalarType.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::array Types = {
    cparser::ScalarType{.name = "char", .size = 1},
    cparser::ScalarType{.name = "short", .size = 2},
    cparser::ScalarType{.name = "int", .size = 4},
    cparser::ScalarType{.name = "long", .size = 4},
    cparser::ScalarType{.name = "signed", .size = 4},
    cparser::ScalarType{.name = "unsigned", .size = 4, .isSigned = false},
};

constexpr std::array Aggregates = {cparser::ScalarType{.name = "Vtx", .size = 16}};

constexpr cparser::Dialect TestDialect{.types = Types, .builtins = {}, .aggregates = Aggregates};

std::optional<cparser::SymbolAddress> findSymbol(const std::string_view name) {
    if (name == "sVertices") {
        return cparser::SymbolAddress{.address = 0x06000100, .elementSize = 16};
    }
    return std::nullopt;
}

/// Evaluates the last line of `source` against the macros the lines before it
/// define, with `sVertices` an array of 16-byte vertices at 0x06000100.
struct Evaluation {
    explicit Evaluation(std::string source)
        : unit(cparser::preprocessText(base, "memory.c", std::move(source), {})) {
        const cparser::EvalContext context{
            .macros = &unit.macros, .dialect = &TestDialect, .findSymbol = &findSymbol};
        value = cparser::evaluate(std::span(unit.tokens).first(unit.tokens.size() - 1), context,
                                  diagnostics);
    }

    [[nodiscard]] cparser::s64 getInteger() const {
        REQUIRE(value.has_value());
        return cparser::getInteger(*value);
    }

    cparser::MacroTable base;
    cparser::TranslationUnit unit;
    std::vector<base::Diagnostic> diagnostics;
    std::expected<cparser::Value, base::Diagnostic> value;
};

} // namespace

TEST_CASE("a symbol is its address, and moves by whole elements", "[cparser][address]") {
    REQUIRE(Evaluation("sVertices\n").getInteger() == 0x06000100);
    REQUIRE(Evaluation("sVertices + 3\n").getInteger() == 0x06000130);
    REQUIRE(Evaluation("&sVertices[3]\n").getInteger() == 0x06000130);
    REQUIRE(Evaluation("&sVertices[3] - sVertices\n").getInteger() == 3);
}

TEST_CASE("casting a pointer to an integer stops the scaling", "[cparser][address]") {
    REQUIRE(Evaluation("(unsigned int)(sVertices) + 3\n").getInteger() == 0x06000103);
    REQUIRE(Evaluation("(Vtx*)0x100 + 1\n").getInteger() == 0x110);
}

TEST_CASE("type names may take several words", "[cparser][address]") {
    REQUIRE(Evaluation("(unsigned char)0x1FF\n").getInteger() == 0xFF);
    REQUIRE(Evaluation("(unsigned int)-1\n").getInteger() == 0xFFFF'FFFF);
    REQUIRE(Evaluation("(signed short)0xFFFF\n").getInteger() == -1);
    REQUIRE(Evaluation("sizeof(long long)\n").getInteger() == 8);
    REQUIRE(Evaluation("sizeof(const unsigned long)\n").getInteger() == 4);
}

TEST_CASE("sizeof answers the dialect's aggregates", "[cparser][address]") {
    REQUIRE(Evaluation("sizeof(Vtx) * 4\n").getInteger() == 64);
    REQUIRE(Evaluation("sizeof(Vtx*)\n").getInteger() == 4);
    REQUIRE_FALSE(Evaluation("(Vtx)1\n").value.has_value());
}

TEST_CASE("a macro that pastes tokens evaluates", "[cparser][address]") {
    const Evaluation evaluation("#define G_IM_SIZ_16b_BYTES 2\n"
                                "#define BYTES(siz) siz##_BYTES\n"
                                "BYTES(G_IM_SIZ_16b) * 8\n");

    REQUIRE(evaluation.getInteger() == 16);
    REQUIRE(evaluation.diagnostics.empty());
}
