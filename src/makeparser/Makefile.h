#pragma once

#include "base/Diagnostic.h"
#include "makeparser/MakeVariables.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace makeparser {

/// The `VAR=value` arguments of a make command line.
using MakeOverrides = std::map<std::string, std::string, std::less<>>;

/// What a Makefile leaves defined once `make` has read it, before any rule
/// runs.
struct Makefile {
    MakeVariables variables;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads a Makefile the way GNU make does before it runs a rule: variable
/// assignments of every flavour, conditionals, `define` blocks and includes.
/// Rules are stepped over and their recipes never read. Nothing is executed,
/// so whatever came from `$(shell)` is empty and marked uncertain.
///
/// `overrides` behave as on make's command line: the Makefile's own
/// assignments to those names are ignored unless written with `override`.
/// Includes resolve against the directory holding `file`, which is where make
/// would be run.
[[nodiscard]] std::expected<Makefile, base::Diagnostic>
readMakefile(const std::filesystem::path& file, const MakeOverrides& overrides);

} // namespace makeparser
