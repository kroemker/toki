#include "z64/Scanner.h"

#include "z64/Diagnostic.h"
#include "z64/SourceFile.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Scans a string as a file of its own. The tokens view into `file`, so the
/// whole thing is built in place and never copied.
class Scanned {
public:
    explicit Scanned(std::string_view source)
        : file{.path = "memory.c", .contents = std::string(source)}, result(z64::scan(file)) {}

    [[nodiscard]] std::span<const z64::Token> tokens() const {
        REQUIRE(result.diagnostics.empty());
        return std::span(result.tokens).first(result.tokens.size() - 1);
    }

    [[nodiscard]] std::vector<std::string> texts() const {
        std::vector<std::string> texts;
        for (const z64::Token& token : tokens()) {
            texts.emplace_back(token.text);
        }
        return texts;
    }

    z64::SourceFile file;
    z64::ScanResult result;
};

[[nodiscard]] std::vector<std::string> textsOf(std::string_view source) {
    return Scanned(source).texts();
}

} // namespace

TEST_CASE("comments are trivia", "[z64][scanner]") {
    const std::vector<std::string> texts = textsOf("a /* one\ntwo */ b // three\nc");

    REQUIRE(texts == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("numbers keep their decomp spelling", "[z64][scanner]") {
    const Scanned scanned("0x0A 010 42u 0xFFFFFFFFuL 1.0f .5f 2.5e-3f");
    const std::span<const z64::Token> tokens = scanned.tokens();

    REQUIRE(tokens.size() == 7);
    for (const z64::usize at : {0U, 1U, 2U, 3U}) {
        REQUIRE(tokens[at].kind == z64::TokenKind::Integer);
    }
    for (const z64::usize at : {4U, 5U, 6U}) {
        REQUIRE(tokens[at].kind == z64::TokenKind::Real);
    }
    REQUIRE(tokens[3].text == "0xFFFFFFFFuL");
    REQUIRE(tokens[6].text == "2.5e-3f");
}

TEST_CASE("multi-character punctuators are one token", "[z64][scanner]") {
    REQUIRE(textsOf("a << 3 | b >> 1") ==
            std::vector<std::string>{"a", "<<", "3", "|", "b", ">>", "1"});
    REQUIRE(textsOf("x->y") == std::vector<std::string>{"x", "->", "y"});
}

TEST_CASE("string and character literals stay whole", "[z64][scanner]") {
    const std::string quoted = R"("a \"quoted\" string")";
    const std::string character = "'\\0'";
    const std::string source = "a " + quoted + " " + character;
    const Scanned scanned(source);
    const std::span<const z64::Token> tokens = scanned.tokens();

    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[1].kind == z64::TokenKind::String);
    REQUIRE(tokens[1].text == quoted);
    REQUIRE(tokens[2].kind == z64::TokenKind::Character);
    REQUIRE(tokens[2].text == character);
}

TEST_CASE("an angled include target is one token", "[z64][scanner]") {
    const std::vector<std::string> texts = textsOf("#include <ultra64.h>");

    REQUIRE(texts == std::vector<std::string>{"#", "include", "<ultra64.h>"});
}

TEST_CASE("line starts survive a continuation", "[z64][scanner]") {
    const Scanned scanned("#define A(x) \\\n    ((x) + 1)\nB");
    const std::span<const z64::Token> tokens = scanned.tokens();

    const auto lineStarts =
        std::ranges::count_if(tokens, [](const z64::Token& token) { return token.startsLine; });
    REQUIRE(lineStarts == 2);
    REQUIRE(tokens.back().text == "B");
    REQUIRE(tokens.back().startsLine);
}

TEST_CASE("a function-like macro is told apart by the space before its paren", "[z64][scanner]") {
    REQUIRE_FALSE(Scanned("#define A(x) x").tokens()[3].spaceBefore);
    REQUIRE(Scanned("#define A (x)").tokens()[3].spaceBefore);
}

TEST_CASE("every token points at the file it was read from", "[z64][scanner]") {
    const Scanned scanned("a b");

    for (const z64::Token& token : scanned.result.tokens) {
        REQUIRE(token.file == &scanned.file);
    }
}

TEST_CASE("an unterminated block comment is reported", "[z64][scanner]") {
    const z64::SourceFile file{.path = "memory.c", .contents = "a /* never ends"};
    const z64::ScanResult result = z64::scan(file);

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().severity == z64::Severity::Error);
}

TEST_CASE("an exponent without a sign still makes a real", "[z64][scanner]") {
    const Scanned scanned("1e3 2E-4 5e+2f 0xE+1");
    const std::span<const z64::Token> tokens = scanned.tokens();

    REQUIRE(tokens.size() == 6);
    REQUIRE(tokens[0].kind == z64::TokenKind::Real);
    REQUIRE(tokens[1].kind == z64::TokenKind::Real);
    REQUIRE(tokens[2].kind == z64::TokenKind::Real);
    REQUIRE(tokens[2].text == "5e+2f");
    REQUIRE(tokens[3].kind == z64::TokenKind::Integer);
    REQUIRE(tokens[3].text == "0xE");
}

TEST_CASE("a block comment spanning lines does not end the line it started on", "[z64][scanner]") {
    const Scanned scanned("#define A 1 /* spans\n lines */ + 2\nB");
    const std::span<const z64::Token> tokens = scanned.tokens();

    REQUIRE(tokens.size() == 7);
    REQUIRE(tokens[4].text == "+");
    REQUIRE_FALSE(tokens[4].startsLine);
    REQUIRE(tokens[6].text == "B");
    REQUIRE(tokens[6].startsLine);
}
