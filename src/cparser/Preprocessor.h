#pragma once

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/IncludeResolver.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceSet.h"
#include "cparser/Spellings.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {

/// A file an include resolved to that is not read as C. `tag` is whatever the
/// `MissingInclude` that produced it said the file is.
struct EmbeddedFile {
    std::filesystem::path path;
    std::string tag;
    base::SourceLocation location;
};

/// One file's C after directives are gone: every reachable file's tokens in
/// source order, the embedded files the includes resolved to, and everything
/// that went wrong on the way. Each embedded file also stands in `tokens` as an
/// `Embedded` token where its include was, so the declaration it fills holds it
/// in its initializer. Macro uses are left standing, because which of them to
/// expand is the consumer's decision.
///
/// A unit owns what it read. `sources` holds every file it opened, and
/// `macros` holds the definitions those files made, layered over the base the
/// unit was read against. Tokens borrow from both the unit's files and the
/// base's, so the base must outlive the unit. Moving a unit is safe.
struct TranslationUnit {
    SourceSet sources;
    MacroTable macros;

    /// The text of tokens no file spelled, made while expanding as the unit
    /// was read (`PreprocessOptions::expandMacros`).
    Spellings spellings;

    std::vector<Token> tokens;
    std::vector<EmbeddedFile> embedded;
    std::vector<base::Diagnostic> diagnostics;

    /// The spellings of quoted includes that resolved to no file the unit may
    /// follow, each once, in the order first met. A caller that reads those
    /// headers another way knows from them which.
    std::vector<std::string> unresolvedIncludes;
};

struct PreprocessOptions {
    /// Tried after the including file's own directory. Extracted assets spell
    /// their includes from the repository root, so that root belongs here.
    std::vector<std::filesystem::path> searchPaths;

    /// An include is followed only when it resolves inside one of these. Empty
    /// means no restriction.
    std::vector<std::filesystem::path> allowedRoots;

    usize maxDepth = 32;

    /// Asked about an include that names no file on disk, before it is
    /// reported missing.
    MissingInclude resolveMissingInclude;

    /// The type names and builtins conditions are evaluated with. Plain C when
    /// nothing is given.
    const Dialect* dialect = nullptr;

    /// Identifiers an `#if` may use that are not macros, such as the enum
    /// constants the ordinal tables generate.
    std::function<std::optional<Value>(std::string_view)> resolve;

    /// Expands text as it is read, against the macros defined at that point,
    /// as a compiler's preprocessor does. Headers that define a macro, include
    /// an X-macro table with it and undefine it again read right only this
    /// way. Asset reading leaves it off, because which macros to expand there
    /// is the consumer's decision.
    bool expandMacros = false;

    /// Raises an unresolved identifier in a conditional from a note to a
    /// warning. Ordinal tables set it: there, a condition read wrongly shifts
    /// every following entry's ID, so it must never pass quietly.
    bool strictConditionals = false;

    /// Told of every `#define` in a branch the conditionals take, with the
    /// directive it was read at, as it is defined. An editor of configuration
    /// headers finds by it which of a name's lines the build reads.
    std::function<void(const Macro& macro, const Token& directive)> onDefine;
};

/// Reads `entry` and everything it reaches, evaluating conditionals against
/// the unit's own macros layered over `base`. `base` is never written to, so
/// reading the same file twice reads it the same way. Only a failure to read
/// the entry file itself is an error; everything past that is a diagnostic, so
/// a scene with one bad include still produces the rest of its data.
[[nodiscard]] std::expected<TranslationUnit, base::Diagnostic>
preprocess(const MacroTable& base, const std::filesystem::path& entry,
           const PreprocessOptions& options);

/// The same, for an entry that exists only in memory. Its includes resolve
/// against the directory `path` names, as they would if it were on disk.
[[nodiscard]] TranslationUnit preprocessText(const MacroTable& base,
                                             const std::filesystem::path& path,
                                             std::string contents,
                                             const PreprocessOptions& options);

} // namespace cparser
