#pragma once

#include <deque>
#include <string>
#include <string_view>

namespace cparser {

/// Owns the text of tokens no file spelled: the ones `##` pastes and `#`
/// stringifies. A token's text is a view, so whatever makes such a token keeps
/// its text here, and the set must outlive the token. Moving the set is safe.
class Spellings {
public:
    [[nodiscard]] std::string_view keep(std::string text) {
        return texts.emplace_back(std::move(text));
    }

private:
    std::deque<std::string> texts;
};

} // namespace cparser
