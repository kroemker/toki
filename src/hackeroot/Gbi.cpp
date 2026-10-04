#include "Gbi.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/Scrape.h"
#include "cparser/SourceFile.h"
#include "cparser/SourceSet.h"
#include "cparser/Value.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view GbiDirectory = "include/ultra64";
constexpr std::array<std::string_view, 2> GbiHeaders{"gbi.h", "gbi.f3dex3.h"};
constexpr std::string_view ProbeMacro = "gsSPVertex";

bool isEnabled(const cparser::MacroTable& macros, const std::string_view name) {
    const cparser::Macro* macro = macros.find(name);
    if (macro == nullptr || macro->functionLike) {
        return false;
    }
    std::vector<base::Diagnostic> ignored;
    const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
        macro->body, cparser::EvalContext{.macros = &macros, .dialect = &z64::getDecompDialect()},
        ignored);
    return value && cparser::isTrue(*value);
}

void hideOtherArms(cparser::MacroTable& layer, const cparser::MacroTable& read,
                   const cparser::SourceFile& file) {
    for (const std::string_view name : cparser::findMentionedNames(file)) {
        if (!read.contains(name)) {
            layer.remove(name);
        }
    }
}

} // namespace

std::expected<Gbi, base::Diagnostic> readGbi(const Workspace& workspace) {
    const std::filesystem::path& root = workspace.getRoot();
    const std::filesystem::path directory = root / GbiDirectory;
    if (isEnabled(workspace.getMacros(), "ENABLE_F3DEX3")) {
        return std::unexpected(base::makeError(
            {.file = root / "include" / "config", .line = 0, .column = 0},
            "the checkout enables F3DEX3, which Toki64 does not draw; only F3DEX2 is supported"));
    }

    cparser::MacroTable configuration;
    configuration.defineFrom(workspace.getMacros());
    cparser::TranslationUnit unit = cparser::preprocessText(
        configuration, root / "include" / "<gbi>", "#include \"ultra64/gbi.h\"\n",
        cparser::PreprocessOptions{.searchPaths = {root / "include"},
                                   .dialect = &z64::getDecompDialect()});
    const std::vector<std::string_view> defined = unit.macros.getDefinedNames();
    if (std::ranges::find(defined, ProbeMacro) == defined.end()) {
        return std::unexpected(base::makeError(
            {.file = directory / GbiHeaders.front(), .line = 0, .column = 0},
            "gbi.h defines no gs* macros under the build's configuration; the Makefile should "
            "pass -DF3DEX_GBI_2"));
    }

    Gbi gbi{.macros = cparser::MacroTable(&workspace.getMacros())};
    gbi.macros.defineFrom(unit.macros);
    for (const cparser::SourceFile* file : unit.sources.getFiles()) {
        hideOtherArms(gbi.macros, unit.macros, *file);
    }
    for (const std::string_view header : GbiHeaders) {
        if (const std::expected<const cparser::SourceFile*, base::Diagnostic> file =
                unit.sources.open(directory / header)) {
            hideOtherArms(gbi.macros, unit.macros, **file);
        }
    }
    gbi.sources = std::move(unit.sources);
    gbi.diagnostics = std::move(unit.diagnostics);
    return gbi;
}

} // namespace hackeroot
