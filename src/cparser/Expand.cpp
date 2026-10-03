#include "Expand.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

namespace {

using HideSet = std::vector<std::string_view>;

struct Pending {
    Token token;
    HideSet hidden;
};

using Argument = std::vector<Pending>;

struct Call {
    std::vector<Argument> arguments;
    HideSet closingHidden;
};

[[nodiscard]] bool hides(const HideSet& set, std::string_view name) {
    return std::ranges::find(set, name) != set.end();
}

[[nodiscard]] HideSet unite(HideSet set, const HideSet& more) {
    for (const std::string_view name : more) {
        if (!hides(set, name)) {
            set.push_back(name);
        }
    }
    return set;
}

[[nodiscard]] HideSet intersect(const HideSet& left, const HideSet& right) {
    HideSet both;
    for (const std::string_view name : left) {
        if (hides(right, name)) {
            both.push_back(name);
        }
    }
    return both;
}

class Expander {
public:
    Expander(const ExpandOptions& options, std::vector<base::Diagnostic>& diagnostics)
        : options(options), diagnostics(diagnostics) {}

    [[nodiscard]] std::vector<Pending> run(std::vector<Pending> input);

private:
    [[nodiscard]] const Macro* lookup(const Pending& pending) const;

    [[nodiscard]] std::optional<Call> readCall(std::vector<Pending>& stack, const Macro& macro,
                                               const Token& use);

    void fitArguments(std::vector<Argument>& arguments, const Macro& macro, const Token& use);

    [[nodiscard]] std::vector<Pending> substitute(const Macro& macro,
                                                  const std::vector<Argument>& arguments,
                                                  const HideSet& hidden, const Token& use);

    const ExpandOptions& options;
    std::vector<base::Diagnostic>& diagnostics;
    usize expansions = 0;
    bool exhausted = false;
};

const Macro* Expander::lookup(const Pending& pending) const {
    const Token& token = pending.token;
    if (token.kind != TokenKind::Identifier || options.macros == nullptr) {
        return nullptr;
    }
    if (hides(pending.hidden, token.text) || (options.keep && options.keep(token.text))) {
        return nullptr;
    }
    return options.macros->find(token.text);
}

std::optional<Call> Expander::readCall(std::vector<Pending>& stack, const Macro& macro,
                                       const Token& use) {
    std::vector<Pending> consumed;
    consumed.push_back(std::move(stack.back()));
    stack.pop_back();

    Call call;
    Argument current;
    usize nesting = 0;
    const auto collectingRest = [&macro](const Call& sofar) {
        return macro.variadic && sofar.arguments.size() + 1 >= macro.parameters.size();
    };
    while (!stack.empty()) {
        Pending pending = std::move(stack.back());
        stack.pop_back();
        consumed.push_back(pending);

        const Token& token = pending.token;
        if (isPunctuator(token, "(")) {
            ++nesting;
        } else if (isPunctuator(token, ")")) {
            if (nesting == 0) {
                call.arguments.push_back(std::move(current));
                call.closingHidden = std::move(pending.hidden);
                return call;
            }
            --nesting;
        } else if (isPunctuator(token, ",") && nesting == 0 && !collectingRest(call)) {
            call.arguments.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(std::move(pending));
    }

    diagnostics.push_back(base::warning(
        locate(use), std::format("the arguments to '{}' are never closed", macro.name)));
    while (!consumed.empty()) {
        stack.push_back(std::move(consumed.back()));
        consumed.pop_back();
    }
    return std::nullopt;
}

void Expander::fitArguments(std::vector<Argument>& arguments, const Macro& macro,
                            const Token& use) {
    const usize expected = macro.parameters.size();
    if (expected == 0 && arguments.size() == 1 && arguments.front().empty()) {
        arguments.clear();
    }
    if (macro.variadic && arguments.size() + 1 == expected) {
        arguments.emplace_back();
    }
    if (arguments.size() != expected) {
        diagnostics.push_back(
            base::warning(locate(use), std::format("'{}' takes {} arguments but {} were given",
                                                   macro.name, expected, arguments.size())));
        arguments.resize(expected);
    }
}

std::vector<Pending> Expander::substitute(const Macro& macro,
                                          const std::vector<Argument>& arguments,
                                          const HideSet& hidden, const Token& use) {
    std::vector<std::vector<Pending>> expanded(arguments.size());
    std::vector<bool> isExpanded(arguments.size(), false);
    std::vector<Pending> output;
    bool reportedOperator = false;

    for (const Token& token : macro.body) {
        if (isPunctuator(token, "#") || isPunctuator(token, "##")) {
            if (!reportedOperator) {
                diagnostics.push_back(base::warning(
                    locate(use), std::format("'{}' uses the '{}' operator, which is not supported",
                                             macro.name, token.text)));
                reportedOperator = true;
            }
            continue;
        }

        const auto parameter = std::ranges::find(macro.parameters, token.text);
        if (token.kind != TokenKind::Identifier || parameter == macro.parameters.end()) {
            output.push_back(Pending{.token = token, .hidden = hidden});
            continue;
        }

        const auto index = static_cast<usize>(parameter - macro.parameters.begin());
        if (!isExpanded[index]) {
            expanded[index] = run(arguments[index]);
            isExpanded[index] = true;
        }
        for (const Pending& pending : expanded[index]) {
            output.push_back(
                Pending{.token = pending.token, .hidden = unite(pending.hidden, hidden)});
        }
    }
    return output;
}

std::vector<Pending> Expander::run(std::vector<Pending> input) {
    std::ranges::reverse(input);
    std::vector<Pending> output;

    while (!input.empty()) {
        Pending current = std::move(input.back());
        input.pop_back();

        const Macro* macro = exhausted ? nullptr : lookup(current);
        if (macro == nullptr) {
            output.push_back(std::move(current));
            continue;
        }
        if (++expansions > options.maxExpansions) {
            diagnostics.push_back(base::error(
                locate(current.token), std::format("macro expansion did not finish after {} steps",
                                                   options.maxExpansions)));
            exhausted = true;
            output.push_back(std::move(current));
            continue;
        }

        HideSet hidden;
        std::vector<Argument> arguments;
        if (macro->functionLike) {
            if (input.empty() || !isPunctuator(input.back().token, "(")) {
                output.push_back(std::move(current));
                continue;
            }
            std::optional<Call> call = readCall(input, *macro, current.token);
            if (!call) {
                output.push_back(std::move(current));
                continue;
            }
            arguments = std::move(call->arguments);
            fitArguments(arguments, *macro, current.token);
            hidden = intersect(current.hidden, call->closingHidden);
        } else {
            hidden = current.hidden;
        }
        hidden = unite(std::move(hidden), HideSet{macro->name});

        std::vector<Pending> replacement = substitute(*macro, arguments, hidden, current.token);
        if (!replacement.empty()) {
            replacement.front().token.startsLine = current.token.startsLine;
            replacement.front().token.spaceBefore = current.token.spaceBefore;
        }
        for (auto pending = replacement.rbegin(); pending != replacement.rend(); ++pending) {
            input.push_back(std::move(*pending));
        }
    }
    return output;
}

} // namespace

std::vector<Token> expand(std::span<const Token> tokens, const ExpandOptions& options,
                          std::vector<base::Diagnostic>& diagnostics) {
    std::vector<Pending> input;
    input.reserve(tokens.size());
    for (const Token& token : tokens) {
        input.push_back(Pending{.token = token});
    }

    std::vector<Token> output;
    for (const Pending& pending : Expander(options, diagnostics).run(std::move(input))) {
        output.push_back(pending.token);
    }
    return output;
}

} // namespace cparser
