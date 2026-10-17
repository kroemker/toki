#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// Variables read off one line written as make's command line takes them.
struct MakeVariables {
    std::map<std::string, std::string, std::less<>> variables;
    /// The words of the line that are not `NAME=value`, as written.
    std::vector<std::string> rejected;
};

/// Reads `line` as `NAME=value` words split by spaces, such as
/// `VERSION=gc-eu-mq DEBUG_FEATURES=1`. A double-quoted stretch keeps its
/// spaces, so a value may hold them. A name given twice keeps the last value,
/// as make does.
[[nodiscard]] MakeVariables parseMakeVariables(std::string_view line);

/// `variables` as one line that `parseMakeVariables` reads back the same,
/// quoting a value that holds a space.
[[nodiscard]] std::string
formatMakeVariables(const std::map<std::string, std::string, std::less<>>& variables);

} // namespace toki::core
