#include "Gbi.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Value.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Workspace.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view GbiHeader = "ultra64/gbi.h";
constexpr std::array<std::string_view, 1> AlsoHidden{"ultra64/gbi.f3dex3.h"};
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

} // namespace

std::optional<base::Diagnostic> findUnsupportedMicrocode(const Workspace& workspace) {
    if (!isEnabled(workspace.getMacros(), "ENABLE_F3DEX3")) {
        return std::nullopt;
    }
    return base::makeError(
        {.file = workspace.getRoot() / "include" / "config" / "config_graphics.h",
         .line = 0,
         .column = 0},
        "the checkout enables F3DEX3, which Toki64 does not draw; only F3DEX2 is supported. Set "
        "ENABLE_F3DEX3 to false in include/config/config_graphics.h, or build with "
        "CPP_DEFINES=-DENABLE_F3DEX3=0");
}

std::expected<Prelude, base::Diagnostic> readGbi(const Workspace& workspace) {
    if (std::optional<base::Diagnostic> unsupported = findUnsupportedMicrocode(workspace)) {
        return std::unexpected(std::move(*unsupported));
    }
    const std::filesystem::path& root = workspace.getRoot();

    const std::array<std::string, 1> includes{std::string(GbiHeader)};
    Prelude gbi = readPrelude(workspace, includes, AlsoHidden);
    const std::vector<std::string_view> defined = gbi.macros.getDefinedNames();
    if (std::ranges::find(defined, ProbeMacro) == defined.end()) {
        return std::unexpected(base::makeError(
            {.file = root / "include" / GbiHeader, .line = 0, .column = 0},
            "gbi.h defines no gs* macros under the build's configuration; the Makefile should "
            "pass -DF3DEX_GBI_2"));
    }
    return gbi;
}

} // namespace hackeroot
