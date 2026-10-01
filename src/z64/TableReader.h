#pragma once

#include "z64/Diagnostic.h"
#include "z64/Preprocessor.h"
#include "z64/SourceSet.h"
#include "z64/Types.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace z64 {

class MacroTable;

struct TableRow {
    usize ordinal = 0;
    std::string_view macro;
    std::vector<std::string_view> arguments;
    SourceLocation location;
};

/// The rows' spellings view into `sources`, which the result owns, so moving a
/// result is safe and the rows live exactly as long as it does.
struct TableResult {
    SourceSet sources;
    std::vector<TableRow> rows;
    std::vector<Diagnostic> diagnostics;
};

/// Reads an X-macro table into ordinal rows. `SceneID` and `ActorID` are the
/// position of an entry in its table, so a conditionally-disabled entry counted
/// as live shifts every ID after it. That is why this preprocesses rather than
/// pattern-matching: `#if OOT_DEBUG` around one object entry is real, and the
/// answer comes from the checkout's own `config/` headers.
[[nodiscard]] std::expected<TableResult, Diagnostic>
readTable(const MacroTable& base, const std::filesystem::path& file,
          std::span<const std::string_view> entryMacros, const PreprocessOptions& options);

} // namespace z64
