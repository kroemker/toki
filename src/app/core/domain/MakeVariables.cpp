#include "MakeVariables.h"

#include "app/core/Types.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

struct Word {
    std::string text;
    std::string written;
};

[[nodiscard]] std::vector<Word> splitWords(const std::string_view line) {
    std::vector<Word> words;
    usize at = 0;
    while (at < line.size()) {
        if (line[at] == ' ' || line[at] == '\t') {
            ++at;
            continue;
        }
        const usize start = at;
        Word word;
        bool isQuoted = false;
        while (at < line.size() && (isQuoted || (line[at] != ' ' && line[at] != '\t'))) {
            if (line[at] == '"') {
                isQuoted = !isQuoted;
            } else {
                word.text.push_back(line[at]);
            }
            ++at;
        }
        word.written = std::string(line.substr(start, at - start));
        words.push_back(std::move(word));
    }
    return words;
}

[[nodiscard]] bool isVariableName(const std::string_view name) {
    return !name.empty() && name.find_first_of(":#\"") == std::string_view::npos;
}

} // namespace

MakeVariables parseMakeVariables(const std::string_view line) {
    MakeVariables read;
    for (Word& word : splitWords(line)) {
        const usize equals = word.text.find('=');
        const std::string_view name = std::string_view(word.text).substr(0, equals);
        if (equals == std::string::npos || !isVariableName(name)) {
            read.rejected.push_back(std::move(word.written));
            continue;
        }
        read.variables.insert_or_assign(std::string(name), word.text.substr(equals + 1));
    }
    return read;
}

std::string formatMakeVariables(const std::map<std::string, std::string, std::less<>>& variables) {
    std::string line;
    for (const auto& [name, value] : variables) {
        if (!line.empty()) {
            line.push_back(' ');
        }
        line += name;
        line.push_back('=');
        const bool isQuoted = value.find_first_of(" \t") != std::string::npos;
        if (isQuoted) {
            line.push_back('"');
        }
        line += value;
        if (isQuoted) {
            line.push_back('"');
        }
    }
    return line;
}

} // namespace toki::core
