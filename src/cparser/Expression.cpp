#include "Expression.h"

#include "base/Diagnostic.h"
#include "cparser/Expand.h"
#include "cparser/Token.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <expected>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

namespace {

[[nodiscard]] bool isArrayCount(std::string_view name) {
    return name == "ARRAY_COUNT" || name == "ARRAY_COUNTU" || name == "ARRAY_COUNT_2D";
}

[[nodiscard]] bool isBuiltinCall(std::string_view name) {
    return isArrayCount(name) || name == "TEX_LEN";
}

[[nodiscard]] int precedenceOf(std::string_view op) {
    if (op == "*" || op == "/" || op == "%") {
        return 10;
    }
    if (op == "+" || op == "-") {
        return 9;
    }
    if (op == "<<" || op == ">>") {
        return 8;
    }
    if (op == "<" || op == ">" || op == "<=" || op == ">=") {
        return 7;
    }
    if (op == "==" || op == "!=") {
        return 6;
    }
    if (op == "&") {
        return 5;
    }
    if (op == "^") {
        return 4;
    }
    if (op == "|") {
        return 3;
    }
    if (op == "&&") {
        return 2;
    }
    if (op == "||") {
        return 1;
    }
    return 0;
}

struct Cast {
    std::string_view type;
    bool pointer = false;
    usize after = 0;
};

class Evaluator {
public:
    Evaluator(std::span<const Token> tokens, const EvalContext& context,
              std::vector<base::Diagnostic>& diagnostics)
        : tokens(tokens), context(context), diagnostics(diagnostics) {
        if (!tokens.empty()) {
            const Token& last = tokens.back();
            endOfInput.file = last.file;
            endOfInput.line = last.line;
            endOfInput.column = last.column + static_cast<u32>(last.text.size());
        }
    }

    [[nodiscard]] std::expected<Value, base::Diagnostic> run();

private:
    [[nodiscard]] bool atEnd() const {
        return cursor >= tokens.size() || tokens[cursor].kind == TokenKind::EndOfFile;
    }

    [[nodiscard]] const Token& peek() const {
        return atEnd() ? endOfInput : tokens[cursor];
    }

    [[nodiscard]] base::SourceLocation here() const;

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseConditional();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseBinary(int minPrecedence);

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseUnary();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parsePrimary();

    [[nodiscard]] std::optional<Cast> castAt(usize at) const;

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseBuiltin(const Token& name);

    [[nodiscard]] std::expected<std::vector<Value>, base::Diagnostic> parseArguments();

    [[nodiscard]] Value resolveIdentifier(const Token& token);

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseQuietly(bool quietly, auto parse) {
        quiet += quietly ? 1 : 0;
        std::expected<Value, base::Diagnostic> value = parse();
        quiet -= quietly ? 1 : 0;
        return value;
    }

    void report(base::Diagnostic diagnostic) {
        if (quiet == 0) {
            diagnostics.push_back(std::move(diagnostic));
        }
    }

    std::span<const Token> tokens;
    const EvalContext& context;
    std::vector<base::Diagnostic>& diagnostics;
    usize cursor = 0;
    usize quiet = 0;
    Token endOfInput;
};

base::SourceLocation Evaluator::here() const {
    return locate(peek());
}

Value Evaluator::resolveIdentifier(const Token& token) {
    if (token.text == "true") {
        return integerValue(1);
    }
    if (token.text == "false" || token.text == "NULL") {
        return integerValue(0);
    }
    if (token.text == "M_PI") {
        return doubleValue(std::numbers::pi);
    }
    if (context.resolve) {
        if (const std::optional<Value> resolved = context.resolve(token.text)) {
            return *resolved;
        }
    }

    const base::SourceLocation location = locate(token);
    report(
        context.unknownIsZero
            ? base::note(location, std::format("'{}' is not defined, reading it as 0", token.text))
            : base::warning(location,
                            std::format("'{}' is not defined, reading it as 0", token.text)));
    return integerValue(0);
}

std::expected<std::vector<Value>, base::Diagnostic> Evaluator::parseArguments() {
    std::vector<Value> arguments;
    ++cursor;
    if (isPunctuator(peek(), ")")) {
        ++cursor;
        return arguments;
    }
    while (true) {
        const std::expected<Value, base::Diagnostic> argument = parseConditional();
        if (!argument) {
            return std::unexpected(argument.error());
        }
        arguments.push_back(*argument);
        if (isPunctuator(peek(), ",")) {
            ++cursor;
            continue;
        }
        break;
    }
    if (!isPunctuator(peek(), ")")) {
        return std::unexpected(base::error(here(), "expected ')' to close the argument list"));
    }
    ++cursor;
    return arguments;
}

std::expected<Value, base::Diagnostic> Evaluator::parseBuiltin(const Token& name) {
    if (isArrayCount(name.text)) {
        if (cursor + 1 >= tokens.size() || tokens[cursor + 1].kind != TokenKind::Identifier) {
            return std::unexpected(base::error(here(), "ARRAY_COUNT expects an array name"));
        }
        const Token& array = tokens[cursor + 1];
        cursor += 2;
        if (!isPunctuator(peek(), ")")) {
            return std::unexpected(
                base::error(here(), "expected ')' after the ARRAY_COUNT argument"));
        }
        ++cursor;

        if (context.arrayLength) {
            if (const std::optional<usize> length = context.arrayLength(array.text)) {
                return integerValue(static_cast<s64>(*length));
            }
        }
        report(base::warning(
            here(), std::format("ARRAY_COUNT({}) has no declaration to count", array.text)));
        return integerValue(0);
    }

    if (cursor + 2 >= tokens.size() || tokens[cursor + 1].kind != TokenKind::Identifier ||
        !isPunctuator(tokens[cursor + 2], ",")) {
        return std::unexpected(base::error(here(), "TEX_LEN expects an element type first"));
    }
    const s64 elementSize = sizeOfType(tokens[cursor + 1].text);
    cursor += 2;

    std::vector<Value> arguments;
    while (isPunctuator(peek(), ",")) {
        ++cursor;
        const std::expected<Value, base::Diagnostic> argument = parseConditional();
        if (!argument) {
            return std::unexpected(argument.error());
        }
        arguments.push_back(*argument);
    }
    if (!isPunctuator(peek(), ")")) {
        return std::unexpected(base::error(here(), "expected ')' to close TEX_LEN"));
    }
    ++cursor;
    if (arguments.size() != 3) {
        return std::unexpected(base::error(here(),
                                           "TEX_LEN expects a type, a width, a height and a "
                                           "bit depth"));
    }
    const s64 bits = asInteger(arguments[0]) * asInteger(arguments[1]) * asInteger(arguments[2]);
    return integerValue(bits / 8 / elementSize);
}

std::optional<Cast> Evaluator::castAt(usize at) const {
    if (at + 2 >= tokens.size() || tokens[at + 1].kind != TokenKind::Identifier ||
        !isCastType(tokens[at + 1].text)) {
        return std::nullopt;
    }

    usize after = at + 2;
    while (after < tokens.size() && isPunctuator(tokens[after], "*")) {
        ++after;
    }
    if (after >= tokens.size() || !isPunctuator(tokens[after], ")")) {
        return std::nullopt;
    }
    return Cast{.type = tokens[at + 1].text, .pointer = after != at + 2, .after = after + 1};
}

std::expected<Value, base::Diagnostic> Evaluator::parsePrimary() {
    const Token& token = peek();
    if (atEnd()) {
        return std::unexpected(base::error(here(), "the expression ends early"));
    }

    if (isPunctuator(token, "(")) {
        if (const std::optional<Cast> cast = castAt(cursor)) {
            cursor = cast->after;
            const std::expected<Value, base::Diagnostic> operand = parseUnary();
            if (!operand) {
                return operand;
            }
            return castTo(cast->type, cast->pointer, *operand);
        }

        ++cursor;
        const std::expected<Value, base::Diagnostic> inner = parseConditional();
        if (!inner) {
            return inner;
        }
        if (!isPunctuator(peek(), ")")) {
            return std::unexpected(base::error(here(), "expected ')'"));
        }
        ++cursor;
        return inner;
    }

    if (token.kind == TokenKind::Identifier) {
        if (isBuiltinCall(token.text) && cursor + 1 < tokens.size() &&
            isPunctuator(tokens[cursor + 1], "(")) {
            ++cursor;
            return parseBuiltin(token);
        }
        ++cursor;
        if (isPunctuator(peek(), "(")) {
            const std::expected<std::vector<Value>, base::Diagnostic> arguments = parseArguments();
            if (!arguments) {
                return std::unexpected(arguments.error());
            }
            report(base::warning(
                here(),
                std::format("'{}' is not a macro this build knows, reading it as 0", token.text)));
            return integerValue(0);
        }
        return resolveIdentifier(token);
    }

    if (const std::optional<Value> literal = literalValue(token)) {
        ++cursor;
        return *literal;
    }
    return std::unexpected(
        base::error(here(), std::format("'{}' is not a constant expression", token.text)));
}

std::expected<Value, base::Diagnostic> Evaluator::parseUnary() {
    const Token& token = peek();
    if (token.kind != TokenKind::Punctuator) {
        return parsePrimary();
    }

    const std::string_view op = token.text;
    if (op != "-" && op != "+" && op != "~" && op != "!" && op != "&") {
        return parsePrimary();
    }

    ++cursor;
    const std::expected<Value, base::Diagnostic> operand = parseUnary();
    if (!operand) {
        return operand;
    }
    if (op == "+" || op == "&") {
        return *operand;
    }
    if (op == "-") {
        if (isReal(*operand)) {
            Value negated = *operand;
            negated.number = -negated.number;
            return negated;
        }
        return integerValue(static_cast<s64>(u64{0} - static_cast<u64>(operand->integer)));
    }
    if (op == "~") {
        return integerValue(~asInteger(*operand));
    }
    return integerValue(isTrue(*operand) ? 0 : 1);
}

std::expected<Value, base::Diagnostic> Evaluator::parseBinary(int minPrecedence) {
    std::expected<Value, base::Diagnostic> left = parseUnary();
    if (!left) {
        return left;
    }

    while (!atEnd() && peek().kind == TokenKind::Punctuator) {
        const std::string_view op = peek().text;
        const int precedence = precedenceOf(op);
        if (precedence == 0 || precedence < minPrecedence) {
            break;
        }
        const base::SourceLocation location = here();
        ++cursor;
        const bool decided = (op == "&&" && !isTrue(*left)) || (op == "||" && isTrue(*left));
        const std::expected<Value, base::Diagnostic> right =
            parseQuietly(decided, [&] { return parseBinary(precedence + 1); });
        if (!right) {
            return right;
        }
        const bool integerDivision = op == "%" || (op == "/" && !isReal(*left) && !isReal(*right));
        if (integerDivision && asInteger(*right) == 0) {
            report(base::warning(location, std::format("'{}' by zero, reading it as 0", op)));
        }
        left = applyOperator(op, *left, *right);
    }
    return left;
}

std::expected<Value, base::Diagnostic> Evaluator::parseConditional() {
    const std::expected<Value, base::Diagnostic> condition = parseBinary(1);
    if (!condition || !isPunctuator(peek(), "?")) {
        return condition;
    }

    ++cursor;
    const bool chosen = isTrue(*condition);
    const std::expected<Value, base::Diagnostic> whenTrue =
        parseQuietly(!chosen, [this] { return parseConditional(); });
    if (!whenTrue) {
        return whenTrue;
    }
    if (!isPunctuator(peek(), ":")) {
        return std::unexpected(base::error(here(), "expected ':' in a conditional expression"));
    }
    ++cursor;
    const std::expected<Value, base::Diagnostic> whenFalse =
        parseQuietly(chosen, [this] { return parseConditional(); });
    if (!whenFalse) {
        return whenFalse;
    }
    return chosen ? *whenTrue : *whenFalse;
}

std::expected<Value, base::Diagnostic> Evaluator::run() {
    const std::expected<Value, base::Diagnostic> value = parseConditional();
    if (value && !atEnd()) {
        return std::unexpected(
            base::error(here(), std::format("unexpected '{}' after the expression", peek().text)));
    }
    return value;
}

} // namespace

std::expected<Value, base::Diagnostic> evaluate(std::span<const Token> tokens,
                                                const EvalContext& context,
                                                std::vector<base::Diagnostic>& diagnostics) {
    const ExpandOptions options{
        .macros = context.macros,
        .keep =
            [&context](std::string_view name) {
                return isBuiltinCall(name) || (context.keep && context.keep(name));
            },
    };
    const std::vector<Token> expanded = expand(tokens, options, diagnostics);
    return Evaluator(expanded, context, diagnostics).run();
}

} // namespace cparser
