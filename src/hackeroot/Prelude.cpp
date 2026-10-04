#include "Prelude.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/Scrape.h"
#include "cparser/SourceFile.h"
#include "cparser/SourceSet.h"
#include "cparser/TypeParser.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

void hideOtherArms(cparser::MacroTable& layer, const cparser::MacroTable& read,
                   const cparser::SourceFile& file) {
    for (const std::string_view name : cparser::findMentionedNames(file)) {
        if (!read.contains(name)) {
            layer.remove(name);
        }
    }
}

std::vector<std::filesystem::path> makeIncludePaths(const Workspace& workspace) {
    const std::filesystem::path& root = workspace.getRoot();
    const std::string& version = workspace.getBuildDefines().version;
    return {root / "include", root / "src", root / "build" / version, root,
            root / "extracted" / version};
}

} // namespace

Prelude readPrelude(const Workspace& workspace, const std::span<const std::string> includes) {
    return readPrelude(workspace, includes, {});
}

Prelude readPrelude(const Workspace& workspace, const std::span<const std::string> includes,
                    const std::span<const std::string_view> alsoHidden) {
    std::string source;
    for (const std::string& include : includes) {
        source += std::format("#include \"{}\"\n", include);
    }
    cparser::MacroTable configuration;
    configuration.defineFrom(workspace.getMacros());
    cparser::TranslationUnit unit = cparser::preprocessText(
        configuration, workspace.getRoot() / "<prelude>", std::move(source),
        cparser::PreprocessOptions{.searchPaths = makeIncludePaths(workspace),
                                   .dialect = &z64::getDecompDialect(),
                                   .expandMacros = true});

    Prelude prelude{.macros = cparser::MacroTable(&workspace.getMacros())};
    prelude.macros.defineFrom(unit.macros);
    for (const cparser::SourceFile* file : unit.sources.getFiles()) {
        hideOtherArms(prelude.macros, unit.macros, *file);
    }
    cparser::SourceSet extra;
    for (const std::string_view header : alsoHidden) {
        if (const std::expected<const cparser::SourceFile*, base::Diagnostic> file =
                extra.open(workspace.getRoot() / "include" / header)) {
            hideOtherArms(prelude.macros, unit.macros, **file);
        }
    }

    cparser::TypeParseResult types =
        cparser::parseTypes(unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
    prelude.types = std::move(types.types);
    prelude.diagnostics = unit.diagnostics;
    prelude.diagnostics.insert(prelude.diagnostics.end(), types.diagnostics.begin(),
                               types.diagnostics.end());
    prelude.sources = std::move(unit.sources);
    return prelude;
}

} // namespace hackeroot
