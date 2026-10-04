#include "Expand.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/Spellings.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

namespace {

using HideSet = u32;

constexpr HideSet NoneHidden = 0;

/// Every hide set one expansion makes, interned, so a token carries an index
/// and copying one never allocates. Unions and intersections are remembered,
/// since the same few sets meet again for every token of a replacement.
class HideSets {
public:
    HideSets() {
        sets.emplace_back();
        ids.emplace(std::vector<std::string_view>{}, NoneHidden);
    }

    [[nodiscard]] bool hides(const HideSet set, const std::string_view name) const {
        return std::ranges::find(sets[set], name) != sets[set].end();
    }

    [[nodiscard]] HideSet add(const HideSet set, const std::string_view name) {
        if (hides(set, name)) {
            return set;
        }
        std::vector<std::string_view> names = sets[set];
        names.push_back(name);
        return intern(std::move(names));
    }

    [[nodiscard]] HideSet unite(const HideSet left, const HideSet right) {
        if (left == right || right == NoneHidden) {
            return left;
        }
        if (left == NoneHidden) {
            return right;
        }
        const auto [found, isNew] = unions.try_emplace({left, right}, NoneHidden);
        if (isNew) {
            std::vector<std::string_view> names = sets[left];
            for (const std::string_view name : sets[right]) {
                if (!hides(left, name)) {
                    names.push_back(name);
                }
            }
            found->second = intern(std::move(names));
        }
        return found->second;
    }

    [[nodiscard]] HideSet intersect(const HideSet left, const HideSet right) {
        if (left == right) {
            return left;
        }
        if (left == NoneHidden || right == NoneHidden) {
            return NoneHidden;
        }
        const auto [found, isNew] = intersections.try_emplace({left, right}, NoneHidden);
        if (isNew) {
            std::vector<std::string_view> names;
            for (const std::string_view name : sets[left]) {
                if (hides(right, name)) {
                    names.push_back(name);
                }
            }
            found->second = intern(std::move(names));
        }
        return found->second;
    }

private:
    [[nodiscard]] HideSet intern(std::vector<std::string_view> names) {
        std::ranges::sort(names);
        const auto [found, isNew] = ids.try_emplace(names, static_cast<HideSet>(sets.size()));
        if (isNew) {
            sets.push_back(std::move(names));
        }
        return found->second;
    }

    std::vector<std::vector<std::string_view>> sets;
    std::map<std::vector<std::string_view>, HideSet> ids;
    std::map<std::pair<HideSet, HideSet>, HideSet> unions;
    std::map<std::pair<HideSet, HideSet>, HideSet> intersections;
};

struct Pending {
    Token token;
    HideSet hidden = NoneHidden;
};

using Argument = std::vector<Pending>;

struct Call {
    std::vector<Argument> arguments;
    HideSet closingHidden = NoneHidden;
};

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

    struct Substitution {
        const Macro& macro;
        const std::vector<Argument>& arguments;
        HideSet hidden;
        std::vector<std::vector<Pending>> expanded;
        std::vector<bool> isExpanded;
        std::vector<Pending> output;
        bool pasteNext = false;
        bool lastWasEmpty = false;
    };

    [[nodiscard]] static std::optional<usize> findParameter(const Macro& macro, const Token& token);

    [[nodiscard]] std::vector<Pending> substitute(const Macro& macro,
                                                  const std::vector<Argument>& arguments,
                                                  HideSet hidden, const Token& use);

    [[nodiscard]] std::vector<Pending> getArgument(Substitution& substitution, usize index,
                                                   bool asWritten);

    void append(Substitution& substitution, std::vector<Pending> piece, bool isVariadic);

    [[nodiscard]] Token stringify(const Argument& argument, const Token& operation) const;

    [[nodiscard]] std::vector<Pending> paste(const Pending& left, const Pending& right);

    const ExpandOptions& options;
    std::vector<base::Diagnostic>& diagnostics;
    HideSets hideSets;
    usize expansions = 0;
    bool exhausted = false;
};

const Macro* Expander::lookup(const Pending& pending) const {
    const Token& token = pending.token;
    if (token.kind != TokenKind::Identifier || options.macros == nullptr) {
        return nullptr;
    }
    if (hideSets.hides(pending.hidden, token.text) || (options.keep && options.keep(token.text))) {
        return nullptr;
    }
    return options.macros->find(token.text);
}

std::optional<Call> Expander::readCall(std::vector<Pending>& stack, const Macro& macro,
                                       const Token& use) {
    usize nesting = 1;
    std::optional<usize> closing;
    for (usize at = stack.size() - 1; at-- > 0;) {
        const Token& token = stack[at].token;
        if (isPunctuator(token, "(")) {
            ++nesting;
        } else if (isPunctuator(token, ")") && --nesting == 0) {
            closing = at;
            break;
        }
    }
    if (!closing) {
        diagnostics.push_back(base::makeWarning(
            locate(use), std::format("the arguments to '{}' are never closed", macro.name)));
        return std::nullopt;
    }

    Call call;
    Argument current;
    nesting = 0;
    const auto collectingRest = [&macro](const Call& sofar) {
        return macro.variadic && sofar.arguments.size() + 1 >= macro.parameters.size();
    };
    for (usize at = stack.size() - 2; at > *closing; --at) {
        const Pending& pending = stack[at];
        const Token& token = pending.token;
        if (isPunctuator(token, "(")) {
            ++nesting;
        } else if (isPunctuator(token, ")")) {
            --nesting;
        } else if (isPunctuator(token, ",") && nesting == 0 && !collectingRest(call)) {
            call.arguments.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(pending);
    }
    call.arguments.push_back(std::move(current));
    call.closingHidden = stack[*closing].hidden;
    stack.resize(*closing);
    return call;
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
            base::makeWarning(locate(use), std::format("'{}' takes {} arguments but {} were given",
                                                       macro.name, expected, arguments.size())));
        arguments.resize(expected);
    }
}

std::optional<usize> Expander::findParameter(const Macro& macro, const Token& token) {
    const auto parameter = std::ranges::find(macro.parameters, token.text);
    if (token.kind != TokenKind::Identifier || parameter == macro.parameters.end()) {
        return std::nullopt;
    }
    return static_cast<usize>(parameter - macro.parameters.begin());
}

std::vector<Pending> Expander::substitute(const Macro& macro,
                                          const std::vector<Argument>& arguments,
                                          const HideSet hidden, const Token& use) {
    Substitution substitution{.macro = macro, .arguments = arguments, .hidden = hidden};
    substitution.expanded.resize(arguments.size());
    substitution.isExpanded.resize(arguments.size(), false);
    bool reportedOperator = false;
    const std::span<const Token> body = macro.body;
    for (usize i = 0; i < body.size(); ++i) {
        const Token& token = body[i];
        const bool isPaste = isPunctuator(token, "##");
        const std::optional<usize> stringified =
            isPunctuator(token, "#") && macro.functionLike && i + 1 < body.size()
                ? findParameter(macro, body[i + 1])
                : std::nullopt;
        const bool isStringify = stringified.has_value();
        if ((isPaste || isStringify) && options.spellings == nullptr) {
            if (!reportedOperator) {
                diagnostics.push_back(base::makeWarning(
                    locate(use), std::format("'{}' uses the '{}' operator, which needs somewhere "
                                             "to keep the tokens it makes",
                                             macro.name, token.text)));
                reportedOperator = true;
            }
            continue;
        }
        if (isPaste) {
            substitution.pasteNext = !substitution.output.empty() || substitution.lastWasEmpty;
            continue;
        }
        if (stringified) {
            ++i;
            append(substitution,
                   {Pending{.token = stringify(arguments[*stringified], token), .hidden = hidden}},
                   false);
            continue;
        }
        if (const std::optional<usize> index = findParameter(macro, token)) {
            const bool pastedAfter = i + 1 < body.size() && isPunctuator(body[i + 1], "##");
            const bool isVariadic = macro.variadic && *index + 1 == macro.parameters.size();
            append(substitution,
                   getArgument(substitution, *index, substitution.pasteNext || pastedAfter),
                   isVariadic);
            continue;
        }
        append(substitution, {Pending{.token = token, .hidden = hidden}}, false);
    }
    return std::move(substitution.output);
}

std::vector<Pending> Expander::getArgument(Substitution& substitution, const usize index,
                                           const bool asWritten) {
    if (!asWritten && !substitution.isExpanded[index]) {
        substitution.expanded[index] = run(substitution.arguments[index]);
        substitution.isExpanded[index] = true;
    }
    const std::vector<Pending>& tokens =
        asWritten ? substitution.arguments[index] : substitution.expanded[index];
    std::vector<Pending> marked;
    marked.reserve(tokens.size());
    for (const Pending& pending : tokens) {
        marked.push_back(Pending{.token = pending.token,
                                 .hidden = hideSets.unite(pending.hidden, substitution.hidden)});
    }
    return marked;
}

void Expander::append(Substitution& substitution, std::vector<Pending> piece,
                      const bool isVariadic) {
    std::vector<Pending>& output = substitution.output;
    const bool pastes = substitution.pasteNext && !substitution.lastWasEmpty;
    const bool afterComma = pastes && isPunctuator(output.back().token, ",");
    if (pastes && isVariadic && afterComma) {
        if (piece.empty()) {
            output.pop_back();
        }
    } else if (pastes && !piece.empty()) {
        std::vector<Pending> pasted = paste(output.back(), piece.front());
        output.pop_back();
        output.insert(output.end(), pasted.begin(), pasted.end());
        piece.erase(piece.begin());
        substitution.pasteNext = false;
        substitution.lastWasEmpty = false;
        output.insert(output.end(), piece.begin(), piece.end());
        return;
    }
    substitution.pasteNext = false;
    substitution.lastWasEmpty = piece.empty() && !pastes;
    output.insert(output.end(), piece.begin(), piece.end());
}

Token Expander::stringify(const Argument& argument, const Token& operation) const {
    std::string text = "\"";
    for (usize i = 0; i < argument.size(); ++i) {
        const Token& token = argument[i].token;
        if (i > 0 && token.spaceBefore) {
            text += ' ';
        }
        const bool quoted = token.kind == TokenKind::String || token.kind == TokenKind::Character;
        for (const char character : token.text) {
            if (quoted && (character == '"' || character == '\\')) {
                text += '\\';
            }
            text += character;
        }
    }
    text += '"';
    Token made = operation;
    made.kind = TokenKind::String;
    made.text = options.spellings->keep(std::move(text));
    return made;
}

std::vector<Pending> Expander::paste(const Pending& left, const Pending& right) {
    const SourceFile scratch{
        .path = {}, .contents = std::string(left.token.text) + std::string(right.token.text)};
    const ScanResult scanned = scan(scratch);
    const auto made = std::ranges::count_if(
        scanned.tokens, [](const Token& token) { return token.kind != TokenKind::EndOfFile; });
    if (made != 1) {
        diagnostics.push_back(base::makeWarning(
            locate(left.token), std::format("pasting '{}' and '{}' does not give one token",
                                            left.token.text, right.token.text)));
        return {left, right};
    }
    Token pasted = left.token;
    pasted.kind = scanned.tokens.front().kind;
    pasted.text = options.spellings->keep(scratch.contents);
    return {Pending{.token = pasted, .hidden = hideSets.intersect(left.hidden, right.hidden)}};
}

std::vector<Pending> Expander::run(std::vector<Pending> input) {
    std::ranges::reverse(input);
    std::vector<Pending> output;

    while (!input.empty()) {
        const Pending current = input.back();
        input.pop_back();

        const Macro* macro = exhausted ? nullptr : lookup(current);
        if (macro == nullptr) {
            output.push_back(current);
            continue;
        }
        if (++expansions > options.maxExpansions) {
            diagnostics.push_back(base::makeError(
                locate(current.token), std::format("macro expansion did not finish after {} steps",
                                                   options.maxExpansions)));
            exhausted = true;
            output.push_back(current);
            continue;
        }

        HideSet hidden = NoneHidden;
        std::vector<Argument> arguments;
        if (macro->functionLike) {
            if (input.empty() || !isPunctuator(input.back().token, "(")) {
                output.push_back(current);
                continue;
            }
            std::optional<Call> call = readCall(input, *macro, current.token);
            if (!call) {
                output.push_back(current);
                continue;
            }
            arguments = std::move(call->arguments);
            fitArguments(arguments, *macro, current.token);
            hidden = hideSets.intersect(current.hidden, call->closingHidden);
        } else {
            hidden = current.hidden;
        }
        hidden = hideSets.add(hidden, macro->name);

        std::vector<Pending> replacement = substitute(*macro, arguments, hidden, current.token);
        if (!replacement.empty()) {
            replacement.front().token.startsLine = current.token.startsLine;
            replacement.front().token.spaceBefore = current.token.spaceBefore;
        }
        for (auto pending = replacement.rbegin(); pending != replacement.rend(); ++pending) {
            input.push_back(*pending);
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
