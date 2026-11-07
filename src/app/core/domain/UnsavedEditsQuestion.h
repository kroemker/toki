#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// What is asked before edits would be thrown away, naming the assets by
/// their tabs' titles and saying what is about to happen, a verb in its
/// `-ing` form: `You have unsaved edits in "Message 0x1234". Do you want to
/// save them before closing?`
[[nodiscard]] std::string makeUnsavedEditsQuestion(const std::vector<std::string>& titles,
                                                   std::string_view doing);

} // namespace toki::core
