#include "Scanner.h"

#include "z64/Diagnostic.h"
#include "z64/SourceFile.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr std::array Punctuators = {
    std::string_view("<<="), std::string_view(">>="), std::string_view("..."),
    std::string_view("->"),  std::string_view("++"),  std::string_view("--"),
    std::string_view("<<"),  std::string_view(">>"),  std::string_view("<="),
    std::string_view(">="),  std::string_view("=="),  std::string_view("!="),
    std::string_view("&&"),  std::string_view("||"),  std::string_view("+="),
    std::string_view("-="),  std::string_view("*="),  std::string_view("/="),
    std::string_view("%="),  std::string_view("&="),  std::string_view("|="),
    std::string_view("^="),  std::string_view("##"),
};

[[nodiscard]] bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool isIdentifierStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] bool isIdentifierBody(char c) {
    return isIdentifierStart(c) || isDigit(c);
}

class Scanner {
public:
    explicit Scanner(const SourceFile& file) : source(file.contents), file(file) {}

    [[nodiscard]] ScanResult run();

private:
    [[nodiscard]] bool atEnd() const {
        return cursor >= source.size();
    }

    [[nodiscard]] char peek(usize ahead = 0) const {
        const usize at = cursor + ahead;
        return at < source.size() ? source[at] : '\0';
    }

    [[nodiscard]] SourceLocation here() const {
        return SourceLocation{.file = file.path, .line = line, .column = column};
    }

    void advance(usize count = 1);

    void skipTrivia();

    void skipComment();

    void push(TokenKind kind, usize begin, u32 beginLine, u32 beginColumn);

    void scanNumber();

    void scanQuoted(char terminator, TokenKind kind);

    void scanPunctuator();

    std::string_view source;
    const SourceFile& file;
    usize cursor = 0;
    u32 line = 1;
    u32 column = 1;
    bool startsLine = true;
    bool spaceBefore = false;
    ScanResult result;
};

void Scanner::advance(usize count) {
    for (usize step = 0; step < count && !atEnd(); ++step) {
        if (source[cursor] == '\n') {
            ++line;
            column = 1;
            startsLine = true;
        } else {
            ++column;
        }
        ++cursor;
    }
}

void Scanner::skipTrivia() {
    while (!atEnd()) {
        const char c = peek();
        if (c == '\\' && (peek(1) == '\n' || (peek(1) == '\r' && peek(2) == '\n'))) {
            const bool wasLineStart = startsLine;
            advance(peek(1) == '\r' ? 3 : 2);
            startsLine = wasLineStart;
            spaceBefore = true;
            continue;
        }
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            advance();
            spaceBefore = true;
            continue;
        }
        if (c == '/' && (peek(1) == '/' || peek(1) == '*')) {
            skipComment();
            spaceBefore = true;
            continue;
        }
        break;
    }
}

void Scanner::skipComment() {
    if (peek(1) == '/') {
        while (!atEnd() && peek() != '\n') {
            advance();
        }
        return;
    }

    const SourceLocation opened = here();
    advance(2);
    while (!atEnd() && (peek() != '*' || peek(1) != '/')) {
        advance();
    }
    if (atEnd()) {
        result.diagnostics.push_back(error(opened, "unterminated block comment"));
        return;
    }
    advance(2);
}

void Scanner::push(TokenKind kind, usize begin, u32 beginLine, u32 beginColumn) {
    result.tokens.push_back(Token{
        .kind = kind,
        .startsLine = startsLine,
        .spaceBefore = spaceBefore,
        .file = &file,
        .text = source.substr(begin, cursor - begin),
        .line = beginLine,
        .column = beginColumn,
    });
    startsLine = false;
    spaceBefore = false;
}

void Scanner::scanNumber() {
    const usize begin = cursor;
    const u32 beginLine = line;
    const u32 beginColumn = column;
    const bool hexadecimal = peek() == '0' && (peek(1) == 'x' || peek(1) == 'X');
    bool fractional = false;

    if (hexadecimal) {
        advance(2);
    }
    while (!atEnd()) {
        const char c = peek();
        if (!hexadecimal && (c == 'e' || c == 'E') && (peek(1) == '+' || peek(1) == '-')) {
            fractional = true;
            advance(2);
            continue;
        }
        if (c == '.') {
            fractional = true;
            advance();
            continue;
        }
        if (!isIdentifierBody(c)) {
            break;
        }
        advance();
    }
    push(fractional ? TokenKind::Real : TokenKind::Integer, begin, beginLine, beginColumn);
}

void Scanner::scanQuoted(char terminator, TokenKind kind) {
    const usize begin = cursor;
    const u32 beginLine = line;
    const u32 beginColumn = column;
    const SourceLocation opened = here();
    advance();
    while (!atEnd() && peek() != terminator && peek() != '\n') {
        advance(peek() == '\\' ? 2 : 1);
    }
    if (peek() != terminator) {
        result.diagnostics.push_back(error(opened, "unterminated literal"));
    } else {
        advance();
    }
    push(kind, begin, beginLine, beginColumn);
}

void Scanner::scanPunctuator() {
    const usize begin = cursor;
    const u32 beginLine = line;
    const u32 beginColumn = column;
    const std::string_view rest = source.substr(cursor);
    for (const std::string_view candidate : Punctuators) {
        if (rest.starts_with(candidate)) {
            advance(candidate.size());
            push(TokenKind::Punctuator, begin, beginLine, beginColumn);
            return;
        }
    }
    advance();
    push(TokenKind::Punctuator, begin, beginLine, beginColumn);
}

ScanResult Scanner::run() {
    result.tokens.reserve(source.size() / 4);
    while (true) {
        skipTrivia();
        if (atEnd()) {
            break;
        }

        const char c = peek();
        const usize begin = cursor;
        const u32 beginLine = line;
        const u32 beginColumn = column;

        if (isIdentifierStart(c)) {
            while (!atEnd() && isIdentifierBody(peek())) {
                advance();
            }
            push(TokenKind::Identifier, begin, beginLine, beginColumn);
        } else if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
            scanNumber();
        } else if (c == '"') {
            scanQuoted('"', TokenKind::String);
        } else if (c == '\'') {
            scanQuoted('\'', TokenKind::Character);
        } else if (c == '<' && !result.tokens.empty() &&
                   isIdentifier(result.tokens.back(), "include")) {
            scanQuoted('>', TokenKind::String);
        } else {
            scanPunctuator();
        }
    }

    result.tokens.push_back(Token{
        .kind = TokenKind::EndOfFile,
        .startsLine = true,
        .file = &file,
        .line = line,
        .column = column,
    });
    return std::move(result);
}

} // namespace

ScanResult scan(const SourceFile& file) {
    return Scanner(file).run();
}

} // namespace z64
