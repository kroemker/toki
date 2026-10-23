#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace toki::core {

/// What the open checkout offers its build settings: the versions extracted
/// into it, and the value make gives each variable the settings leave unset.
struct BuildChoices {
    std::vector<std::string> versions;
    std::map<std::string, std::string, std::less<>> defaults;
};

} // namespace toki::core
