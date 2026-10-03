#pragma once

#include "base/Diagnostic.h"
#include "cparser/Types.h"

#include <string_view>

namespace cparser {

struct SourceFile;

enum class TokenKind : u8 {
    EndOfFile,
    Identifier,
    Integer,
    Real,
    String,
    Character,
    Punctuator,

    /// Stands where an `#include` of texture data was, after the include was
    /// resolved to the image the build would have generated it from. Only the
    /// preprocessor makes these; `Token::image` says which image.
    Image,
};

/// One lexeme of decomp C. `text` is a view into `file->contents`, so a token is
/// only valid while the `SourceSet` that read its file is alive.
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    bool startsLine = false;
    bool spaceBefore = false;

    /// For an `Image` token, its index into `TranslationUnit::images`.
    u32 image = 0;

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

/// Where a token was written. A token that came from no file, such as the
/// end-of-input marker an evaluator makes up, has an empty path.
[[nodiscard]] base::SourceLocation locate(const Token& token);

} // namespace cparser
