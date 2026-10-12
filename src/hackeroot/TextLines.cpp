#include "TextLines.h"

#include "hackeroot/Types.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace hackeroot {

std::string_view trimSpaces(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> splitLines(const std::string_view text) {
    std::vector<std::string_view> lines;
    usize begin = 0;
    while (begin <= text.size()) {
        const usize end = std::min(text.find('\n', begin), text.size());
        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

} // namespace hackeroot
