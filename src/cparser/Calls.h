#pragma once

#include "base/Diagnostic.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

/// A call written in a function body, or a function-like macro used like one:
/// the callee's name and each argument's tokens, unexpanded.
struct Call {
    std::string_view callee;
    std::vector<std::span<const Token>> arguments;
    base::SourceLocation location;
};

/// Every call in `body` to a name `isWanted` accepts, in the order they are
/// written, calls nested in another's arguments included. Nothing is
/// evaluated and nothing is followed: a call through a pointer or a macro that
/// expands to a call is not seen. A member named like a wanted function
/// (`this->Draw(...)`) is not a call to it.
[[nodiscard]] std::vector<Call> findCalls(std::span<const Token> body,
                                          const std::function<bool(std::string_view)>& isWanted);

} // namespace cparser
