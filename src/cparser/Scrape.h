#pragma once

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceSet.h"
#include "cparser/Types.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace cparser {

struct SourceFile;

/// The macros every file in a checkout is read against, and the headers their
/// bodies borrow from. Translation units layer their own macros over
/// `macros`, so a result must outlive, and must not move under, every unit
/// read against it.
struct ScrapeResult {
    SourceSet sources;
    MacroTable macros;
    usize headers = 0;
    usize definitions = 0;
    std::vector<base::Diagnostic> diagnostics;
};

/// Collects every `#define` in a header tree without preprocessing it: a
/// linear scan per file, conditionals ignored, the first definition of a name
/// winning. This is how the parser learns what `SCENE_CMD_ROOM_LIST`,
/// `ARRAY_COUNT` and the `config/` feature flags mean, rather than hard-coding
/// a copy of the decomp that goes stale.
///
/// Both arms of an `#if` are seen, which is the price of not preprocessing.
/// It costs one wrong body for a name defined differently per configuration,
/// and buys not having to resolve the whole include graph to read one scene.
[[nodiscard]] ScrapeResult scrapeDefinitions(const std::filesystem::path& directory);

/// Every name a `#define` or `#undef` in `file` spells, conditionals ignored,
/// in source order. The views point into `file`.
[[nodiscard]] std::vector<std::string_view> findMentionedNames(const SourceFile& file);

} // namespace cparser
