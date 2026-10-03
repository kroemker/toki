#include "MakeSyntax.h"

#include "makeparser/MakeWords.h"
#include "makeparser/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace makeparser {

namespace {

[[nodiscard]] bool endsWithContinuation(std::string_view text) {
    usize backslashes = 0;
    while (backslashes < text.size() && text[text.size() - 1 - backslashes] == '\\') {
        ++backslashes;
    }
    return backslashes % 2 == 1;
}

[[nodiscard]] MakeStatement assignmentAt(std::string_view text, usize equals) {
    MakeAssign assign = MakeAssign::Recursive;
    usize nameEnd = equals;
    if (equals > 0) {
        switch (text[equals - 1]) {
        case ':':
            assign = MakeAssign::Simple;
            nameEnd = equals >= 2 && text[equals - 2] == ':' ? equals - 2 : equals - 1;
            break;
        case '+':
            assign = MakeAssign::Append;
            nameEnd = equals - 1;
            break;
        case '?':
            assign = MakeAssign::Conditional;
            nameEnd = equals - 1;
            break;
        case '!':
            assign = MakeAssign::Shell;
            nameEnd = equals - 1;
            break;
        default:
            break;
        }
    }
    return MakeStatement{.kind = MakeStatementKind::Assignment,
                         .name = trim(text.substr(0, nameEnd)),
                         .value = trimLeft(text.substr(equals + 1)),
                         .assign = assign};
}

} // namespace

std::vector<MakeLine> splitMakeLines(std::string_view text) {
    std::vector<std::string_view> physical;
    for (usize begin = 0; begin <= text.size();) {
        usize end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(begin, end - begin);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        physical.push_back(line);
        begin = end + 1;
    }

    std::vector<MakeLine> lines;
    for (usize at = 0; at < physical.size(); ++at) {
        MakeLine line{.text = std::string(physical[at]),
                      .number = static_cast<u32>(at + 1),
                      .recipe = physical[at].starts_with('\t')};
        while (endsWithContinuation(line.text) && at + 1 < physical.size()) {
            line.text.pop_back();
            while (!line.text.empty() && (line.text.back() == ' ' || line.text.back() == '\t')) {
                line.text.pop_back();
            }
            line.text += ' ';
            line.text += trimLeft(physical[++at]);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string_view stripMakeComment(std::string_view text) {
    for (usize at = 0; at < text.size(); ++at) {
        if (text[at] == '#' && (at == 0 || text[at - 1] != '\\')) {
            return text.substr(0, at);
        }
    }
    return text;
}

std::pair<std::string_view, std::string_view> firstWord(std::string_view text) {
    text = trimLeft(text);
    const usize end = text.find_first_of(" \t");
    if (end == std::string_view::npos) {
        return {text, {}};
    }
    return {text.substr(0, end), trimLeft(text.substr(end))};
}

MakeStatement classifyStatement(std::string_view text) {
    usize depth = 0;
    for (usize at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c == '(' || c == '{') {
            ++depth;
        } else if ((c == ')' || c == '}') && depth > 0) {
            --depth;
        } else if (depth > 0) {
            continue;
        } else if (c == '=') {
            return assignmentAt(text, at);
        } else if (c == ':') {
            const bool simple =
                (at + 1 < text.size() && text[at + 1] == '=') ||
                (at + 2 < text.size() && text[at + 1] == ':' && text[at + 2] == '=');
            if (!simple) {
                return MakeStatement{.kind = MakeStatementKind::Rule};
            }
        }
    }
    return MakeStatement{};
}

std::optional<std::pair<std::string_view, std::string_view>>
comparisonOperands(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (text.front() == '(') {
        const usize close = findClose(text, 0);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view inner = text.substr(1, close - 1);
        const usize comma = findTopLevel(inner, ',');
        if (comma == std::string_view::npos) {
            return std::nullopt;
        }
        return std::pair{trim(inner.substr(0, comma)), trim(inner.substr(comma + 1))};
    }

    const auto quoted = [](std::string_view& rest) -> std::optional<std::string_view> {
        rest = trimLeft(rest);
        if (rest.empty() || (rest.front() != '"' && rest.front() != '\'')) {
            return std::nullopt;
        }
        const usize end = rest.find(rest.front(), 1);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view value = rest.substr(1, end - 1);
        rest.remove_prefix(end + 1);
        return value;
    };
    const std::optional<std::string_view> first = quoted(text);
    const std::optional<std::string_view> second = quoted(text);
    if (!first || !second) {
        return std::nullopt;
    }
    return std::pair{*first, *second};
}

} // namespace makeparser
