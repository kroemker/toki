#pragma once

#include "z64/Diagnostic.h"
#include "z64/Token.h"

#include <vector>

namespace z64 {

struct SourceFile;

struct ScanResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

/// Lexes decomp C into tokens, dropping comments and line continuations.
/// Preprocessor directives are not interpreted here: `#` comes back as an
/// ordinary punctuator carrying `startsLine`, and `Preprocessor` acts on it.
/// Every token points at `file`, which must outlive them.
[[nodiscard]] ScanResult scan(const SourceFile& file);

} // namespace z64
