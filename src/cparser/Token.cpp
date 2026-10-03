#include "Token.h"

#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"

#include <filesystem>

namespace cparser {

base::SourceLocation locate(const Token& token) {
    return base::SourceLocation{
        .file = token.file == nullptr ? std::filesystem::path() : token.file->path,
        .line = token.line,
        .column = token.column,
    };
}

} // namespace cparser
