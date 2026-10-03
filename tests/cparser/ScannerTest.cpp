#include "cparser/Scanner.h"

#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

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
        : file{.path = "memory.c", .contents = std::string(source)}, result(cparser::scan(file)) {}

    [[nodiscard]] std::span<const cparser::Token> getTokens() const {
        REQUIRE(result.diagnostics.empty());
        return std::span(result.tokens).first(result.tokens.size() - 1);
    }

    [[nodiscard]] std::vector<std::string> getTexts() const {
        std::vector<std::string> texts;
        for (const cparser::Token& token : getTokens()) {
            texts.emplace_back(token.text);
        }
        return texts;
    }

    cparser::SourceFile file;
    cparser::ScanResult result;
};

[[nodiscard]] std::vector<std::string> scanTexts(std::string_view source) {
    return Scanned(source).getTexts();
}

} // namespace

TEST_CASE("comments are trivia", "[cparser][scanner]") {
    const std::vector<std::string> texts = scanTexts("a /* one\ntwo */ b // three\nc");

    REQUIRE(texts == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("numbers keep their decomp spelling", "[cparser][scanner]") {
    const Scanned scanned("0x0A 010 42u 0xFFFFFFFFuL 1.0f .5f 2.5e-3f");
    const std::span<const cparser::Token> tokens = scanned.getTokens();

    REQUIRE(tokens.size() == 7);
    for (const cparser::usize at : {0U, 1U, 2U, 3U}) {
        REQUIRE(tokens[at].kind == cparser::TokenKind::Integer);
    }
    for (const cparser::usize at : {4U, 5U, 6U}) {
        REQUIRE(tokens[at].kind == cparser::TokenKind::Real);
    }
    REQUIRE(tokens[3].text == "0xFFFFFFFFuL");
    REQUIRE(tokens[6].text == "2.5e-3f");
}

TEST_CASE("multi-character punctuators are one token", "[cparser][scanner]") {
    REQUIRE(scanTexts("a << 3 | b >> 1") ==
            std::vector<std::string>{"a", "<<", "3", "|", "b", ">>", "1"});
    REQUIRE(scanTexts("x->y") == std::vector<std::string>{"x", "->", "y"});
}

TEST_CASE("string and character literals stay whole", "[cparser][scanner]") {
    const std::string quoted = R"("a \"quoted\" string")";
    const std::string character = "'\\0'";
    const std::string source = "a " + quoted + " " + character;
    const Scanned scanned(source);
    const std::span<const cparser::Token> tokens = scanned.getTokens();

    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[1].kind == cparser::TokenKind::String);
    REQUIRE(tokens[1].text == quoted);
    REQUIRE(tokens[2].kind == cparser::TokenKind::Character);
    REQUIRE(tokens[2].text == character);
}

TEST_CASE("an angled include target is one token", "[cparser][scanner]") {
    const std::vector<std::string> texts = scanTexts("#include <ultra64.h>");

    REQUIRE(texts == std::vector<std::string>{"#", "include", "<ultra64.h>"});
}

TEST_CASE("line starts survive a continuation", "[cparser][scanner]") {
    const Scanned scanned("#define A(x) \\\n    ((x) + 1)\nB");
    const std::span<const cparser::Token> tokens = scanned.getTokens();

    const auto lineStarts =
        std::ranges::count_if(tokens, [](const cparser::Token& token) { return token.startsLine; });
    REQUIRE(lineStarts == 2);
    REQUIRE(tokens.back().text == "B");
    REQUIRE(tokens.back().startsLine);
}

TEST_CASE("a function-like macro is told apart by the space before its paren",
          "[cparser][scanner]") {
    REQUIRE_FALSE(Scanned("#define A(x) x").getTokens()[3].spaceBefore);
    REQUIRE(Scanned("#define A (x)").getTokens()[3].spaceBefore);
}

TEST_CASE("every token points at the file it was read from", "[cparser][scanner]") {
    const Scanned scanned("a b");

    for (const cparser::Token& token : scanned.result.tokens) {
        REQUIRE(token.file == &scanned.file);
    }
}

TEST_CASE("an unterminated block comment is reported", "[cparser][scanner]") {
    const cparser::SourceFile file{.path = "memory.c", .contents = "a /* never ends"};
    const cparser::ScanResult result = cparser::scan(file);

    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().severity == base::Severity::Error);
}

TEST_CASE("an exponent without a sign still makes a real", "[cparser][scanner]") {
    const Scanned scanned("1e3 2E-4 5e+2f 0xE+1");
    const std::span<const cparser::Token> tokens = scanned.getTokens();

    REQUIRE(tokens.size() == 6);
    REQUIRE(tokens[0].kind == cparser::TokenKind::Real);
    REQUIRE(tokens[1].kind == cparser::TokenKind::Real);
    REQUIRE(tokens[2].kind == cparser::TokenKind::Real);
    REQUIRE(tokens[2].text == "5e+2f");
    REQUIRE(tokens[3].kind == cparser::TokenKind::Integer);
    REQUIRE(tokens[3].text == "0xE");
}

TEST_CASE("a block comment spanning lines does not end the line it started on",
          "[cparser][scanner]") {
    const Scanned scanned("#define A 1 /* spans\n lines */ + 2\nB");
    const std::span<const cparser::Token> tokens = scanned.getTokens();

    REQUIRE(tokens.size() == 7);
    REQUIRE(tokens[4].text == "+");
    REQUIRE_FALSE(tokens[4].startsLine);
    REQUIRE(tokens[6].text == "B");
    REQUIRE(tokens[6].startsLine);
}
