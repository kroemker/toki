#pragma once

#include "z64/Types.h"

#include <string_view>

namespace z64 {

struct SourceFile;

enum class TokenKind : u8 {
    EndOfFile,
    Identifier,
    Integer,
    Real,
    String,
    Character,
    Punctuator,
};

/// One lexeme of decomp C. `text` is a view into `file->contents`, so a token is
/// only valid while the `SourceSet` that read its file is alive.
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    bool startsLine = false;
    bool spaceBefore = false;
    const SourceFile* file = nullptr;
    std::string_view text;
    u32 line = 0;
    u32 column = 0;
};

[[nodiscard]] inline bool isPunctuator(const Token& token, std::string_view spelling) {
    return token.kind == TokenKind::Punctuator && token.text == spelling;
}

[[nodiscard]] inline bool isIdentifier(const Token& token, std::string_view spelling) {
    return token.kind == TokenKind::Identifier && token.text == spelling;
}

} // namespace z64
