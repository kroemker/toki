#pragma once

#include "makeparser/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace makeparser {

enum class MakeAssign : u8 {
    Recursive,
    Simple,
    Conditional,
    Append,
    Shell,
};

enum class MakeStatementKind : u8 {
    Assignment,
    Rule,
    Other,
};

struct MakeStatement {
    MakeStatementKind kind = MakeStatementKind::Other;
    std::string_view name;
    std::string_view value;
    MakeAssign assign = MakeAssign::Recursive;
};

/// One logical line of a Makefile: continuations joined, numbered by the
/// physical line it starts on, and marked when it starts with a tab, which
/// after a rule makes it part of a recipe.
struct MakeLine {
    std::string text;
    u32 number = 0;
    bool recipe = false;
};

[[nodiscard]] std::vector<MakeLine> splitMakeLines(std::string_view text);

[[nodiscard]] std::string_view stripMakeComment(std::string_view text);

[[nodiscard]] std::pair<std::string_view, std::string_view> firstWord(std::string_view text);

/// Tells an assignment from a rule the way make does, by whichever of `=` and
/// a lone `:` comes first outside a variable reference.
[[nodiscard]] MakeStatement classifyStatement(std::string_view text);

/// The two operands of `ifeq`, in either the `(a,b)` or the quoted form.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>>
comparisonOperands(std::string_view text);

} // namespace makeparser
