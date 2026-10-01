#include "ShellWords.h"

#include "hackeroot/Types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

[[nodiscard]] bool escapesInDoubleQuotes(char c) {
    return c == '"' || c == '\\' || c == '$' || c == '`';
}

class Splitter {
public:
    explicit Splitter(std::string_view text) : text(text) {}

    [[nodiscard]] std::vector<std::string> run();

private:
    void endWord();

    [[nodiscard]] usize singleQuoted(usize open);

    [[nodiscard]] usize doubleQuoted(usize open);

    std::string_view text;
    std::vector<std::string> words;
    std::string word;
    bool inWord = false;
};

void Splitter::endWord() {
    if (inWord) {
        words.push_back(std::move(word));
        word.clear();
        inWord = false;
    }
}

usize Splitter::singleQuoted(usize open) {
    const usize close = text.find('\'', open + 1);
    const usize end = close == std::string_view::npos ? text.size() : close;
    word += text.substr(open + 1, end - open - 1);
    return end;
}

usize Splitter::doubleQuoted(usize open) {
    usize at = open + 1;
    for (; at < text.size() && text[at] != '"'; ++at) {
        if (text[at] == '\\' && at + 1 < text.size() && escapesInDoubleQuotes(text[at + 1])) {
            ++at;
        }
        word += text[at];
    }
    return at;
}

std::vector<std::string> Splitter::run() {
    for (usize at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c == ' ' || c == '\t' || c == '\n') {
            endWord();
            continue;
        }

        inWord = true;
        if (c == '\'') {
            at = singleQuoted(at);
        } else if (c == '"') {
            at = doubleQuoted(at);
        } else if (c == '\\' && at + 1 < text.size()) {
            word += text[++at];
        } else {
            word += c;
        }
    }
    endWord();
    return std::move(words);
}

} // namespace

std::vector<std::string> splitShellWords(std::string_view text) {
    return Splitter(text).run();
}

} // namespace hackeroot
