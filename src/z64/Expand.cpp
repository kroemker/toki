#include "Expand.h"

#include "z64/Diagnostic.h"
#include "z64/MacroTable.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace z64 {

namespace {

class Expander {
public:
    Expander(const ExpandOptions& options, std::vector<Diagnostic>& diagnostics)
        : options(options), diagnostics(diagnostics) {}

    [[nodiscard]] std::vector<Token> run(std::span<const Token> tokens, usize depth);

private:
    [[nodiscard]] bool isHidden(std::string_view name) const {
        return std::ranges::find(hidden, name) != hidden.end();
    }

    [[nodiscard]] const Macro* lookup(const Token& token) const;

    [[nodiscard]] std::vector<std::vector<Token>> readArguments(std::span<const Token> tokens,
                                                                usize& cursor, usize depth);

    [[nodiscard]] std::vector<Token>
    substitute(const Macro& macro, std::span<const std::vector<Token>> arguments, const Token& use);

    const ExpandOptions& options;
    std::vector<Diagnostic>& diagnostics;
    std::vector<std::string_view> hidden;
};

const Macro* Expander::lookup(const Token& token) const {
    if (token.kind != TokenKind::Identifier || options.macros == nullptr) {
        return nullptr;
    }
    if (isHidden(token.text) || (options.keep && options.keep(token.text))) {
        return nullptr;
    }
    return options.macros->find(token.text);
}

std::vector<std::vector<Token>> Expander::readArguments(std::span<const Token> tokens,
                                                        usize& cursor, usize depth) {
    std::vector<std::vector<Token>> arguments;
    std::vector<Token> current;
    usize nesting = 0;

    ++cursor;
    for (; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (isPunctuator(token, "(") || isPunctuator(token, "[") || isPunctuator(token, "{")) {
            ++nesting;
        } else if (isPunctuator(token, "]") || isPunctuator(token, "}")) {
            --nesting;
        } else if (isPunctuator(token, ")")) {
            if (nesting == 0) {
                break;
            }
            --nesting;
        } else if (nesting == 0 && isPunctuator(token, ",")) {
            arguments.push_back(run(current, depth + 1));
            current.clear();
            continue;
        }
        current.push_back(token);
    }

    if (!current.empty() || !arguments.empty()) {
        arguments.push_back(run(current, depth + 1));
    }
    return arguments;
}

std::vector<Token> Expander::substitute(const Macro& macro,
                                        std::span<const std::vector<Token>> arguments,
                                        const Token& use) {
    std::vector<Token> output;
    for (const Token& token : macro.body) {
        if (isPunctuator(token, "#") || isPunctuator(token, "##")) {
            diagnostics.push_back(warning(
                locate(use), std::format("'{}' uses the '{}' operator, which is not supported",
                                         macro.name, token.text)));
            continue;
        }

        const auto parameter = std::ranges::find(macro.parameters, token.text);
        if (token.kind != TokenKind::Identifier || parameter == macro.parameters.end()) {
            output.push_back(token);
            continue;
        }

        const usize index = static_cast<usize>(parameter - macro.parameters.begin());
        if (index < arguments.size()) {
            output.insert(output.end(), arguments[index].begin(), arguments[index].end());
        }
    }
    return output;
}

std::vector<Token> Expander::run(std::span<const Token> tokens, usize depth) {
    if (depth > options.maxDepth) {
        if (!tokens.empty()) {
            diagnostics.push_back(
                error(locate(tokens.front()), "macro expansion nested too deeply"));
        }
        return {tokens.begin(), tokens.end()};
    }

    std::vector<Token> output;
    output.reserve(tokens.size());

    for (usize cursor = 0; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        const Macro* macro = lookup(token);
        if (macro == nullptr) {
            output.push_back(token);
            continue;
        }

        std::vector<std::vector<Token>> arguments;
        if (macro->functionLike) {
            if (cursor + 1 >= tokens.size() || !isPunctuator(tokens[cursor + 1], "(")) {
                output.push_back(token);
                continue;
            }
            ++cursor;
            arguments = readArguments(tokens, cursor, depth);
            if (arguments.size() != macro->parameters.size()) {
                diagnostics.push_back(
                    warning(locate(token),
                            std::format("'{}' takes {} arguments but {} were given", macro->name,
                                        macro->parameters.size(), arguments.size())));
            }
        }

        const std::vector<Token> body = substitute(*macro, arguments, token);
        hidden.push_back(macro->name);
        std::vector<Token> replaced = run(body, depth + 1);
        hidden.pop_back();

        if (!replaced.empty()) {
            replaced.front().startsLine = token.startsLine;
            replaced.front().spaceBefore = token.spaceBefore;
        }
        output.insert(output.end(), std::make_move_iterator(replaced.begin()),
                      std::make_move_iterator(replaced.end()));
    }
    return output;
}

} // namespace

std::vector<Token> expand(std::span<const Token> tokens, const ExpandOptions& options,
                          std::vector<Diagnostic>& diagnostics) {
    return Expander(options, diagnostics).run(tokens, 0);
}

} // namespace z64
