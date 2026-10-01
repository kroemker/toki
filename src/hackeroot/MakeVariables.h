#pragma once

#include "hackeroot/Types.h"
#include "z64/Diagnostic.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

enum class MakeOrigin : u8 {
    File,
    CommandLine,
    Override,
};

struct MakeVariable {
    std::string value;
    bool recursive = false;
    MakeOrigin origin = MakeOrigin::File;

    /// False when the variable was assigned in a branch whose condition could
    /// not be evaluated, or from a value that could not be.
    bool certain = true;
};

/// Expanded text, and whether everything it came from could be evaluated.
struct MakeText {
    std::string text;
    bool certain = true;
};

/// Where a piece of Makefile text is being expanded, for diagnostics, and
/// whether the branch it sits in could be evaluated.
struct MakeSite {
    z64::SourceLocation location;
    bool certain = true;
};

/// The variables `make` holds while it reads a Makefile, expanded by GNU
/// make's rules. Nothing is ever run: `$(shell)`, `$(wildcard)`, `$(eval)` and
/// their kind expand to nothing and mark the result uncertain, with a note
/// naming them, rather than being guessed at.
class MakeVariables {
public:
    [[nodiscard]] const MakeVariable* find(std::string_view name) const;

    void set(std::string name, MakeVariable variable);

    void remove(std::string_view name);

    [[nodiscard]] MakeText expand(std::string_view text, const MakeSite& site,
                                  std::vector<z64::Diagnostic>& diagnostics) const;

    /// The variable's value split into the words it was written as, each
    /// expanded on its own, so one uncertain word does not taint the rest.
    [[nodiscard]] std::vector<MakeText>
    expandWords(std::string_view name, const MakeSite& site,
                std::vector<z64::Diagnostic>& diagnostics) const;

private:
    std::map<std::string, MakeVariable, std::less<>> variables;
};

} // namespace hackeroot
