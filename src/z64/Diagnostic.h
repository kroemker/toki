#pragma once

#include "z64/Types.h"

#include <filesystem>
#include <string>

namespace z64 {

struct Token;

enum class Severity : u8 {
    Note,
    Warning,
    Error,
};

struct SourceLocation {
    std::filesystem::path file;
    u32 line = 0;
    u32 column = 0;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLocation location;
    std::string message;
};

/// Where a token was written. A token that came from no file, such as the
/// end-of-input marker an evaluator makes up, has an empty path.
[[nodiscard]] SourceLocation locate(const Token& token);

[[nodiscard]] Diagnostic note(SourceLocation location, std::string message);

[[nodiscard]] Diagnostic warning(SourceLocation location, std::string message);

[[nodiscard]] Diagnostic error(SourceLocation location, std::string message);

/// A single line in the `path:line:column: severity: message` shape every
/// compiler uses, so log output is clickable in the same places.
[[nodiscard]] std::string describe(const Diagnostic& diagnostic);

} // namespace z64
