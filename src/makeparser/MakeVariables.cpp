#include "MakeVariables.h"

#include "base/Diagnostic.h"
#include "makeparser/MakeWords.h"
#include "makeparser/Types.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace makeparser {

namespace {

constexpr usize MaxDepth = 256;
constexpr usize Unlimited = std::numeric_limits<usize>::max();

constexpr std::array Functions = {
    std::string_view("subst"),      std::string_view("patsubst"),  std::string_view("strip"),
    std::string_view("findstring"), std::string_view("filter"),    std::string_view("filter-out"),
    std::string_view("sort"),       std::string_view("word"),      std::string_view("wordlist"),
    std::string_view("words"),      std::string_view("firstword"), std::string_view("lastword"),
    std::string_view("dir"),        std::string_view("notdir"),    std::string_view("suffix"),
    std::string_view("basename"),   std::string_view("addsuffix"), std::string_view("addprefix"),
    std::string_view("join"),       std::string_view("wildcard"),  std::string_view("realpath"),
    std::string_view("abspath"),    std::string_view("error"),     std::string_view("warning"),
    std::string_view("info"),       std::string_view("shell"),     std::string_view("origin"),
    std::string_view("flavor"),     std::string_view("let"),       std::string_view("foreach"),
    std::string_view("file"),       std::string_view("call"),      std::string_view("value"),
    std::string_view("eval"),       std::string_view("guile"),     std::string_view("if"),
    std::string_view("or"),         std::string_view("and"),       std::string_view("intcmp"),
};

constexpr std::array Unevaluated = {
    std::string_view("wildcard"), std::string_view("realpath"), std::string_view("abspath"),
    std::string_view("eval"),     std::string_view("file"),     std::string_view("guile"),
    std::string_view("let"),      std::string_view("intcmp"),   std::string_view("wordlist"),
    std::string_view("suffix"),   std::string_view("basename"), std::string_view("join"),
};

[[nodiscard]] bool isOneOf(std::string_view name, std::span<const std::string_view> names) {
    return std::ranges::find(names, name) != names.end();
}

[[nodiscard]] usize getArity(std::string_view function) {
    if (function == "subst" || function == "patsubst" || function == "foreach" ||
        function == "if") {
        return 3;
    }
    if (function == "findstring" || function == "filter" || function == "filter-out" ||
        function == "word" || function == "addprefix" || function == "addsuffix") {
        return 2;
    }
    if (function == "call" || function == "or" || function == "and") {
        return Unlimited;
    }
    return 1;
}

[[nodiscard]] bool isOpening(char c) {
    return c == '(' || c == '{';
}

[[nodiscard]] bool isClosing(char c) {
    return c == ')' || c == '}';
}

[[nodiscard]] std::vector<std::string_view> splitArguments(std::string_view text, usize maximum) {
    std::vector<std::string_view> arguments;
    usize depth = 0;
    usize begin = 0;
    for (usize at = 0; at < text.size(); ++at) {
        if (isOpening(text[at])) {
            ++depth;
        } else if (isClosing(text[at]) && depth > 0) {
            --depth;
        } else if (text[at] == ',' && depth == 0 && arguments.size() + 1 < maximum) {
            arguments.push_back(text.substr(begin, at - begin));
            begin = at + 1;
        }
    }
    arguments.push_back(text.substr(begin));
    return arguments;
}

[[nodiscard]] std::vector<std::string_view> splitRawWords(std::string_view text) {
    std::vector<std::string_view> words;
    usize depth = 0;
    usize begin = 0;
    const auto flush = [&](usize end) {
        if (end > begin) {
            words.push_back(text.substr(begin, end - begin));
        }
        begin = end + 1;
    };
    for (usize at = 0; at < text.size(); ++at) {
        if (isOpening(text[at])) {
            ++depth;
        } else if (isClosing(text[at]) && depth > 0) {
            --depth;
        } else if ((text[at] == ' ' || text[at] == '\t' || text[at] == '\n') && depth == 0) {
            flush(at);
        }
    }
    flush(text.size());
    return words;
}

[[nodiscard]] bool isNumber(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

class Expander {
public:
    Expander(const MakeVariables& variables, const MakeSite& site,
             std::vector<base::Diagnostic>& diagnostics)
        : variables(variables), site(site), diagnostics(diagnostics) {}

    [[nodiscard]] std::string run(std::string_view text);

    [[nodiscard]] bool isCertain() const {
        return certain;
    }

private:
    [[nodiscard]] std::string expandReference(std::string_view inner);

    [[nodiscard]] std::string expandVariable(std::string_view name);

    [[nodiscard]] std::string expandSubstitution(std::string_view name, std::string_view from,
                                                 std::string_view to);

    [[nodiscard]] std::string callFunction(std::string_view name, std::string_view arguments);

    void reportUnevaluated(std::string_view name, std::string_view arguments);

    void reportMessage(std::string_view name, std::string_view arguments);

    [[nodiscard]] std::string evaluateLogic(std::string_view name,
                                            std::span<const std::string_view> arguments);

    [[nodiscard]] std::string inspect(std::string_view name,
                                      std::span<const std::string_view> arguments);

    [[nodiscard]] std::string call(std::span<const std::string_view> arguments);

    [[nodiscard]] std::string expandForeach(std::span<const std::string_view> arguments);

    [[nodiscard]] std::string getOrigin(std::string_view name) const;

    void markUncertain(std::string message);

    const MakeVariables& variables;
    const MakeSite& site;
    std::vector<base::Diagnostic>& diagnostics;
    bool certain = true;
    usize depth = 0;
    std::vector<std::vector<std::string>> callArguments;
    std::vector<std::pair<std::string, std::string>> locals;
    std::vector<std::string> expanding;
};

void Expander::markUncertain(std::string message) {
    certain = false;
    diagnostics.push_back(base::makeNote(site.location, std::move(message)));
}

std::string Expander::run(std::string_view text) {
    if (depth >= MaxDepth) {
        certain = false;
        diagnostics.push_back(
            base::makeError(site.location, "variable expansion nested too deeply"));
        return {};
    }
    ++depth;

    std::string output;
    for (usize at = 0; at < text.size(); ++at) {
        if (text[at] != '$') {
            output += text[at];
            continue;
        }
        if (at + 1 >= text.size()) {
            break;
        }
        const char next = text[at + 1];
        if (next == '$') {
            output += '$';
            ++at;
            continue;
        }
        if (!isOpening(next)) {
            output += expandVariable(text.substr(at + 1, 1));
            ++at;
            continue;
        }
        const usize close = findClose(text, at + 1);
        if (close == std::string_view::npos) {
            certain = false;
            diagnostics.push_back(
                base::makeWarning(site.location, "unterminated variable reference"));
            break;
        }
        output += expandReference(text.substr(at + 2, close - at - 2));
        at = close;
    }

    --depth;
    return output;
}

std::string Expander::expandReference(std::string_view inner) {
    const usize nameEnd = inner.find_first_of(" \t");
    if (nameEnd != std::string_view::npos && isOneOf(inner.substr(0, nameEnd), Functions)) {
        return callFunction(inner.substr(0, nameEnd), trimLeft(inner.substr(nameEnd + 1)));
    }

    const usize colon = findTopLevel(inner, ':');
    if (colon != std::string_view::npos) {
        const std::string_view replacement = inner.substr(colon + 1);
        const usize equals = findTopLevel(replacement, '=');
        if (equals != std::string_view::npos) {
            return expandSubstitution(run(inner.substr(0, colon)),
                                      run(replacement.substr(0, equals)),
                                      run(replacement.substr(equals + 1)));
        }
    }
    return expandVariable(run(inner));
}

std::string Expander::expandVariable(std::string_view name) {
    for (auto local = locals.rbegin(); local != locals.rend(); ++local) {
        if (local->first == name) {
            return local->second;
        }
    }
    if (!callArguments.empty() && isNumber(name)) {
        usize index = 0;
        std::from_chars(name.data(), name.data() + name.size(), index);
        const std::vector<std::string>& arguments = callArguments.back();
        return index < arguments.size() ? arguments[index] : std::string();
    }

    const MakeVariable* found = variables.find(name);
    if (found == nullptr) {
        return {};
    }
    certain = certain && found->certain;
    if (!found->recursive) {
        return found->value;
    }
    if (std::ranges::find(expanding, name) != expanding.end()) {
        certain = false;
        diagnostics.push_back(
            base::makeError(site.location, std::format("'{}' refers to itself", name)));
        return {};
    }
    expanding.emplace_back(name);
    std::string value = run(found->value);
    expanding.pop_back();
    return value;
}

std::string Expander::expandSubstitution(std::string_view name, std::string_view from,
                                         std::string_view to) {
    const std::string pattern = from.contains('%') ? std::string(from) : "%" + std::string(from);
    const std::string replacement = to.contains('%') ? std::string(to) : "%" + std::string(to);
    std::vector<std::string> words;
    const std::string value = expandVariable(name);
    for (const std::string_view word : splitWords(value)) {
        words.push_back(substitutePattern(pattern, replacement, word));
    }
    return joinWords(words);
}

std::string Expander::call(std::span<const std::string_view> arguments) {
    std::vector<std::string> values;
    values.push_back(stripWords(run(arguments.front())));
    for (const std::string_view argument : arguments.subspan(1)) {
        values.push_back(run(argument));
    }

    const MakeVariable* called = variables.find(values.front());
    if (called == nullptr) {
        return {};
    }
    certain = certain && called->certain;
    callArguments.push_back(std::move(values));
    std::string result = called->recursive ? run(called->value) : called->value;
    callArguments.pop_back();
    return result;
}

std::string Expander::expandForeach(std::span<const std::string_view> arguments) {
    if (arguments.size() < 3) {
        return {};
    }
    const std::string name = stripWords(run(arguments[0]));
    const std::string list = run(arguments[1]);
    std::vector<std::string> results;
    for (const std::string_view word : splitWords(list)) {
        locals.emplace_back(name, std::string(word));
        results.push_back(run(arguments[2]));
        locals.pop_back();
    }
    return joinWords(results);
}

std::string Expander::getOrigin(std::string_view name) const {
    if (std::ranges::any_of(locals, [name](const auto& local) { return local.first == name; }) ||
        (!callArguments.empty() && isNumber(name))) {
        return "automatic";
    }
    const MakeVariable* found = variables.find(name);
    if (found == nullptr) {
        return "undefined";
    }
    switch (found->origin) {
    case MakeOrigin::CommandLine:
        return "command line";
    case MakeOrigin::Override:
        return "override";
    case MakeOrigin::File:
        return "file";
    }
    return "file";
}

void Expander::reportUnevaluated(std::string_view name, std::string_view arguments) {
    if (name == "shell") {
        markUncertain(
            std::format("$(shell {}) is not run here, reading it as empty", stripWords(arguments)));
    } else {
        markUncertain(std::format("$({} ...) is not evaluated here, reading it as empty", name));
    }
}

void Expander::reportMessage(std::string_view name, std::string_view arguments) {
    const std::string text = run(arguments);
    if (name == "error") {
        const std::string stop = std::format("the Makefile stops here: {}", text);
        diagnostics.push_back(site.certain && certain ? base::makeWarning(site.location, stop)
                                                      : base::makeNote(site.location, stop));
    }
}

std::string Expander::evaluateLogic(std::string_view name,
                                    std::span<const std::string_view> arguments) {
    if (name == "if") {
        if (!stripWords(run(arguments[0])).empty()) {
            return arguments.size() > 1 ? run(arguments[1]) : std::string();
        }
        return arguments.size() > 2 ? run(arguments[2]) : std::string();
    }

    const bool all = name == "and";
    std::string last;
    for (const std::string_view argument : arguments) {
        last = stripWords(run(argument));
        if (last.empty() == all) {
            return last;
        }
    }
    return all ? last : std::string();
}

std::string Expander::inspect(std::string_view name, std::span<const std::string_view> arguments) {
    const std::string variableName = stripWords(run(arguments[0]));
    if (name == "origin") {
        return getOrigin(variableName);
    }
    const MakeVariable* found = variables.find(variableName);
    if (name == "value") {
        return found == nullptr ? std::string() : found->value;
    }
    if (found == nullptr) {
        return "undefined";
    }
    return found->recursive ? "recursive" : "simple";
}

std::string Expander::callFunction(std::string_view name, std::string_view arguments) {
    if (name == "shell" || isOneOf(name, Unevaluated)) {
        reportUnevaluated(name, arguments);
        return {};
    }
    if (name == "error" || name == "warning" || name == "info") {
        reportMessage(name, arguments);
        return {};
    }

    const std::vector<std::string_view> split = splitArguments(arguments, getArity(name));
    if (name == "if" || name == "or" || name == "and") {
        return evaluateLogic(name, split);
    }
    if (name == "call") {
        return call(split);
    }
    if (name == "foreach") {
        return expandForeach(split);
    }
    if (name == "origin" || name == "flavor" || name == "value") {
        return inspect(name, split);
    }

    std::vector<std::string> values;
    values.reserve(split.size());
    for (const std::string_view argument : split) {
        values.push_back(run(argument));
    }
    return applyTextFunction(name, values).value_or(std::string());
}

} // namespace

const MakeVariable* MakeVariables::find(std::string_view name) const {
    const auto found = variables.find(name);
    return found == variables.end() ? nullptr : &found->second;
}

void MakeVariables::set(std::string name, MakeVariable variable) {
    variables.insert_or_assign(std::move(name), std::move(variable));
}

void MakeVariables::remove(std::string_view name) {
    if (const auto found = variables.find(name); found != variables.end()) {
        variables.erase(found);
    }
}

MakeText MakeVariables::expand(std::string_view text, const MakeSite& site,
                               std::vector<base::Diagnostic>& diagnostics) const {
    Expander expander(*this, site, diagnostics);
    std::string expanded = expander.run(text);
    return MakeText{.text = std::move(expanded), .certain = expander.isCertain()};
}

std::vector<MakeText> MakeVariables::expandWords(std::string_view name, const MakeSite& site,
                                                 std::vector<base::Diagnostic>& diagnostics) const {
    const MakeVariable* found = find(name);
    if (found == nullptr) {
        return {};
    }
    if (!found->recursive) {
        return {MakeText{.text = found->value, .certain = found->certain}};
    }

    std::vector<MakeText> words;
    for (const std::string_view word : splitRawWords(found->value)) {
        MakeText expanded = expand(word, site, diagnostics);
        expanded.certain = expanded.certain && found->certain;
        words.push_back(std::move(expanded));
    }
    return words;
}

} // namespace makeparser
