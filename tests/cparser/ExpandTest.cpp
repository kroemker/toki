#include "cparser/Expand.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/// Preprocesses `source`, whose `#define`s become the unit's macros and whose
/// remaining tokens are expanded against them.
class Expansion {
public:
    explicit Expansion(std::string source, cparser::ExpandOptions options = {})
        : unit(cparser::preprocessText(base, "memory.c", std::move(source), {})) {
        options.macros = &unit.macros;
        const std::span<const cparser::Token> tokens =
            std::span(unit.tokens).first(unit.tokens.size() - 1);
        for (const cparser::Token& token : cparser::expand(tokens, options, diagnostics)) {
            if (!text.empty()) {
                text += ' ';
            }
            text += token.text;
        }
    }

    cparser::MacroTable base;
    cparser::TranslationUnit unit;
    std::vector<base::Diagnostic> diagnostics;
    std::string text;
};

} // namespace

TEST_CASE("a replacement is rescanned together with what follows it", "[cparser][expand]") {
    const Expansion expansion("#define G(x) (x + 1)\n#define F G\nF(1)\n");

    REQUIRE(expansion.text == "( 1 + 1 )");
    REQUIRE(expansion.diagnostics.empty());
}

TEST_CASE("a macro never expands inside its own replacement", "[cparser][expand]") {
    REQUIRE(Expansion("#define X X + 1\nX\n").text == "X + 1");
    REQUIRE(Expansion("#define A B\n#define B A\nA\n").text == "A");
}

TEST_CASE("the standard's f(2)(9) example comes out as the standard says", "[cparser][expand]") {
    const Expansion expansion("#define f(a) a*g\n#define g(a) f(a)\nf(2)(9)\n");

    REQUIRE(expansion.text == "2 * 9 * g");
}

TEST_CASE("arguments are expanded before they are substituted", "[cparser][expand]") {
    REQUIRE(Expansion("#define TWO 2\n#define ID(x) x\nID(TWO)\n").text == "2");
}

TEST_CASE("trailing arguments collect into __VA_ARGS__", "[cparser][expand]") {
    const std::string definition = "#define V(a, ...) a __VA_ARGS__\n";

    REQUIRE(Expansion(definition + "V(1, 2, 3)\n").text == "1 2 , 3");
    REQUIRE(Expansion(definition + "V(1)\n").text == "1");
    REQUIRE(Expansion(definition + "V(1)\n").diagnostics.empty());
}

TEST_CASE("GNU's named variadic parameter works like __VA_ARGS__", "[cparser][expand]") {
    REQUIRE(Expansion("#define G(fmt, args...) fmt args\nG(x, y, z)\n").text == "x y , z");
}

TEST_CASE("empty parentheses are one empty argument", "[cparser][expand]") {
    const Expansion one("#define ONE(x) [x]\nONE()\n");
    REQUIRE(one.text == "[ ]");
    REQUIRE(one.diagnostics.empty());

    const Expansion none("#define NONE() done\nNONE()\n");
    REQUIRE(none.text == "done");
    REQUIRE(none.diagnostics.empty());
}

TEST_CASE("a wrong argument count is reported", "[cparser][expand]") {
    const Expansion expansion("#define ONE(x) [x]\nONE(1, 2)\n");

    REQUIRE(expansion.diagnostics.size() == 1);
    REQUIRE(expansion.diagnostics.front().message.contains("takes 1 arguments but 2"));
}

TEST_CASE("a kept name stays, and its arguments still expand", "[cparser][expand]") {
    const Expansion expansion(
        "#define TWO 2\n#define K(x) gone\nK(TWO)\n",
        cparser::ExpandOptions{.keep = [](std::string_view name) { return name == "K"; }});

    REQUIRE(expansion.text == "K ( 2 )");
}

TEST_CASE("an unclosed call is reported and its tokens kept", "[cparser][expand]") {
    const Expansion expansion("#define ONE(x) [x]\nONE(1\n");

    REQUIRE(expansion.text == "ONE ( 1");
    REQUIRE(expansion.diagnostics.size() == 1);
    REQUIRE(expansion.diagnostics.front().message.contains("never closed"));
}

TEST_CASE("expansion gives up loudly past its budget", "[cparser][expand]") {
    const Expansion expansion("#define A 1\nA A A A A\n",
                              cparser::ExpandOptions{.maxExpansions = 3});

    REQUIRE(expansion.text == "1 1 1 A A");
    REQUIRE(expansion.diagnostics.size() == 1);
    REQUIRE(expansion.diagnostics.front().severity == base::Severity::Error);
}

TEST_CASE("a function-like macro without a call is an ordinary name", "[cparser][expand]") {
    REQUIRE(Expansion("#define F(x) x\nF + 1\n").text == "F + 1");
}
