#include "UnsavedEditsQuestion.h"

#include "app/core/Types.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

std::string makeUnsavedEditsQuestion(const std::vector<std::string>& titles,
                                     const std::string_view doing) {
    std::string named;
    for (usize index = 0; index < titles.size(); ++index) {
        if (index > 0) {
            named += index + 1 == titles.size() ? " and " : ", ";
        }
        named += std::format("\"{}\"", titles[index]);
    }
    return std::format("You have unsaved edits in {}. Do you want to save them before {}?", named,
                       doing);
}

} // namespace toki::core
