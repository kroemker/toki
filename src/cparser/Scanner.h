#pragma once

#include "base/Diagnostic.h"
#include "cparser/Token.h"

#include <vector>

namespace cparser {

struct SourceFile;

struct ScanResult {
    std::vector<Token> tokens;
    std::vector<base::Diagnostic> diagnostics;
};

/// Lexes decomp C into tokens, dropping comments and line continuations.
/// Preprocessor directives are not interpreted here: `#` comes back as an
/// ordinary punctuator carrying `startsLine`, and `Preprocessor` acts on it.
/// Every token points at `file`, which must outlive them.
[[nodiscard]] ScanResult scan(const SourceFile& file);

} // namespace cparser
