#include "Expression.h"

#include "base/Diagnostic.h"
#include "cparser/Dialect.h"
#include "cparser/Expand.h"
#include "cparser/ScalarType.h"
#include "cparser/Spellings.h"
#include "cparser/Token.h"
#include "cparser/TypeName.h"
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

constexpr s64 PointerSize = 4;

[[nodiscard]] const Dialect& getDialect(const EvalContext& context) {
    return context.dialect == nullptr ? getStandardC() : *context.dialect;
}

[[nodiscard]] int getPrecedence(std::string_view op) {
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

class Call;

class Evaluator {
public:
    Evaluator(std::span<const Token> tokens, const EvalContext& context,
              std::vector<base::Diagnostic>& diagnostics, usize quiet = 0)
        : tokens(tokens), context(context), diagnostics(diagnostics), quiet(quiet) {
        if (!tokens.empty()) {
            const Token& last = tokens.back();
            endOfInput.file = last.file;
            endOfInput.line = last.line;
            endOfInput.column = last.column + static_cast<u32>(last.text.size());
        }
    }

    [[nodiscard]] std::expected<Value, base::Diagnostic> run();

private:
    friend class Call;

    [[nodiscard]] bool isAtEnd() const {
        return cursor >= tokens.size() || tokens[cursor].kind == TokenKind::EndOfFile;
    }

    [[nodiscard]] const Token& peek() const {
        return isAtEnd() ? endOfInput : tokens[cursor];
    }

    [[nodiscard]] base::SourceLocation locateHere() const;

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseConditional();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseBinary(int minPrecedence);

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseUnary();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parsePrimary();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseAddress();

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseCast(const TypeName& type);

    void checkRelocation(const Value& operand, const Value& result);

    [[nodiscard]] std::optional<Value> parseSizeof();

    [[nodiscard]] std::optional<TypeName> findTypeName(usize open) const {
        return cparser::findTypeName(tokens, open, getDialect(context), context.findTypeSize);
    }

    [[nodiscard]] std::expected<Value, base::Diagnostic> parseBuiltin(const Builtin& builtin,
                                                                      const Token& name);

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

class Call final : public BuiltinCall {
public:
    Call(Evaluator& evaluator, std::span<const std::span<const Token>> arguments,
         base::SourceLocation location)
        : evaluator(evaluator), arguments(arguments), location(std::move(location)) {}

    [[nodiscard]] std::span<const std::span<const Token>> getArguments() const override {
        return arguments;
    }

    [[nodiscard]] const base::SourceLocation& getLocation() const override {
        return location;
    }

    [[nodiscard]] std::expected<Value, base::Diagnostic>
    evaluate(std::span<const Token> tokens) const override {
        return Evaluator(tokens, evaluator.context, evaluator.diagnostics, evaluator.quiet).run();
    }

    [[nodiscard]] std::optional<usize> findArrayLength(std::string_view name) const override {
        return evaluator.context.findArrayLength ? evaluator.context.findArrayLength(name)
                                                 : std::nullopt;
    }

    [[nodiscard]] const ScalarType* findType(std::string_view name) const override {
        return cparser::findType(getDialect(evaluator.context), name);
    }

    void report(base::Diagnostic diagnostic) const override {
        evaluator.report(std::move(diagnostic));
    }

private:
    Evaluator& evaluator;
    std::span<const std::span<const Token>> arguments;
    base::SourceLocation location;
};

base::SourceLocation Evaluator::locateHere() const {
    return locate(peek());
}

Value Evaluator::resolveIdentifier(const Token& token) {
    if (token.text == "true") {
        return makeInteger(1);
    }
    if (token.text == "false" || token.text == "NULL") {
        return makeInteger(0);
    }
    if (token.text == "M_PI") {
        return makeDouble(std::numbers::pi);
    }
    if (context.resolve) {
        if (const std::optional<Value> resolved = context.resolve(token.text)) {
            return *resolved;
        }
    }
    if (context.findSymbol) {
        if (const std::optional<SymbolAddress> symbol = context.findSymbol(token.text)) {
            Value address = makeInteger(symbol->address);
            address.stride = symbol->elementSize;
            address.symbol = symbol->name.empty() ? token.text : symbol->name;
            return address;
        }
    }

    const base::SourceLocation location = locate(token);
    report(context.unknownIsZero
               ? base::makeNote(location,
                                std::format("'{}' is not defined, reading it as 0", token.text))
               : base::makeWarning(
                     location, std::format("'{}' is not defined, reading it as 0", token.text)));
    return makeInteger(0);
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
        return std::unexpected(
            base::makeError(locateHere(), "expected ')' to close the argument list"));
    }
    ++cursor;
    return arguments;
}

std::expected<Value, base::Diagnostic> Evaluator::parseBuiltin(const Builtin& builtin,
                                                               const Token& name) {
    std::vector<std::span<const Token>> arguments;
    usize nesting = 0;
    usize begin = cursor + 1;
    for (usize at = cursor; at < tokens.size(); ++at) {
        if (isPunctuator(tokens[at], "(")) {
            ++nesting;
        } else if (isPunctuator(tokens[at], ")")) {
            if (--nesting == 0) {
                if (at > begin || !arguments.empty()) {
                    arguments.push_back(tokens.subspan(begin, at - begin));
                }
                cursor = at + 1;
                const Call call(*this, arguments, locate(name));
                return builtin.evaluate(call);
            }
        } else if (isPunctuator(tokens[at], ",") && nesting == 1) {
            arguments.push_back(tokens.subspan(begin, at - begin));
            begin = at + 1;
        }
    }
    return std::unexpected(base::makeError(
        locate(name), std::format("the arguments to '{}' are never closed", name.text)));
}

std::optional<Value> Evaluator::parseSizeof() {
    const Token& token = peek();
    if (token.kind != TokenKind::Identifier || token.text != "sizeof") {
        return std::nullopt;
    }
    const std::optional<TypeName> type = findTypeName(cursor + 1);
    if (!type) {
        return std::nullopt;
    }
    cursor = type->end;
    if (type->pointerDepth > 0) {
        return makeInteger(PointerSize);
    }
    if (type->size == 0) {
        report(base::makeWarning(
            locateHere(), std::format("'{}' has no size, reading it as 0", type->type.name)));
    }
    return makeInteger(static_cast<s64>(type->size));
}

std::expected<Value, base::Diagnostic> Evaluator::parseAddress() {
    const Token& name = peek();
    ++cursor;
    if (!isPunctuator(peek(), "[")) {
        return resolveIdentifier(name);
    }
    const Value array = resolveIdentifier(name);
    ++cursor;
    const std::expected<Value, base::Diagnostic> index = parseConditional();
    if (!index) {
        return index;
    }
    if (!isPunctuator(peek(), "]")) {
        return std::unexpected(base::makeError(locateHere(), "expected ']'"));
    }
    ++cursor;
    if (array.stride == 0) {
        report(base::makeWarning(
            locate(name), std::format("'{}' is not an array, reading &{}[...] as its address",
                                      name.text, name.text)));
        return array;
    }
    return applyOperator("+", array, *index);
}

void Evaluator::checkRelocation(const Value& operand, const Value& result) {
    if (!operand.symbol.empty() && result.symbol.empty()) {
        report(base::makeWarning(
            locateHere(), std::format("the address of '{}' is used in arithmetic that cannot be "
                                      "patched once it is laid out",
                                      operand.symbol)));
    }
}

std::expected<Value, base::Diagnostic> Evaluator::parseCast(const TypeName& type) {
    if (type.isAggregate && type.pointerDepth == 0) {
        return std::unexpected(
            base::makeError(locateHere(), std::format("'{}' cannot be cast to", type.type.name)));
    }
    cursor = type.end;
    const std::expected<Value, base::Diagnostic> operand = parseUnary();
    if (!operand) {
        return operand;
    }
    Value cast = castTo(type.type, type.pointerDepth, *operand);
    if (type.pointerDepth == 1 && type.size > 0) {
        cast.stride = type.size;
    }
    checkRelocation(*operand, cast);
    return cast;
}

std::expected<Value, base::Diagnostic> Evaluator::parsePrimary() {
    const Token& token = peek();
    if (isAtEnd()) {
        return std::unexpected(base::makeError(locateHere(), "the expression ends early"));
    }

    if (isPunctuator(token, "(")) {
        if (const std::optional<TypeName> cast = findTypeName(cursor)) {
            return parseCast(*cast);
        }

        ++cursor;
        const std::expected<Value, base::Diagnostic> inner = parseConditional();
        if (!inner) {
            return inner;
        }
        if (!isPunctuator(peek(), ")")) {
            return std::unexpected(base::makeError(locateHere(), "expected ')'"));
        }
        ++cursor;
        return inner;
    }

    if (const std::optional<Value> size = parseSizeof()) {
        return *size;
    }

    if (token.kind == TokenKind::Identifier) {
        const Builtin* builtin = findBuiltin(getDialect(context), token.text);
        if (builtin != nullptr && cursor + 1 < tokens.size() &&
            isPunctuator(tokens[cursor + 1], "(")) {
            ++cursor;
            return parseBuiltin(*builtin, token);
        }
        ++cursor;
        if (isPunctuator(peek(), "(")) {
            const std::expected<std::vector<Value>, base::Diagnostic> arguments = parseArguments();
            if (!arguments) {
                return std::unexpected(arguments.error());
            }
            report(base::makeWarning(
                locateHere(),
                std::format("'{}' is not a macro this build knows, reading it as 0", token.text)));
            return makeInteger(0);
        }
        return resolveIdentifier(token);
    }

    if (const std::optional<Value> literal = parseLiteral(token)) {
        ++cursor;
        return *literal;
    }
    return std::unexpected(base::makeError(
        locateHere(), std::format("'{}' is not a constant expression", token.text)));
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
    if (op == "&" && peek().kind == TokenKind::Identifier) {
        return parseAddress();
    }
    const std::expected<Value, base::Diagnostic> operand = parseUnary();
    if (!operand) {
        return operand;
    }
    if (op == "+" || op == "&") {
        return *operand;
    }
    checkRelocation(*operand, Value{});
    if (op == "-") {
        if (isReal(*operand)) {
            Value negated = *operand;
            negated.number = -negated.number;
            return negated;
        }
        return makeInteger(static_cast<s64>(u64{0} - static_cast<u64>(operand->integer)));
    }
    if (op == "~") {
        return makeInteger(~getInteger(*operand));
    }
    return makeInteger(isTrue(*operand) ? 0 : 1);
}

std::expected<Value, base::Diagnostic> Evaluator::parseBinary(int minPrecedence) {
    std::expected<Value, base::Diagnostic> left = parseUnary();
    if (!left) {
        return left;
    }

    while (!isAtEnd() && peek().kind == TokenKind::Punctuator) {
        const std::string_view op = peek().text;
        const int precedence = getPrecedence(op);
        if (precedence == 0 || precedence < minPrecedence) {
            break;
        }
        const base::SourceLocation location = locateHere();
        ++cursor;
        const bool decided = (op == "&&" && !isTrue(*left)) || (op == "||" && isTrue(*left));
        const std::expected<Value, base::Diagnostic> right =
            parseQuietly(decided, [&] { return parseBinary(precedence + 1); });
        if (!right) {
            return right;
        }
        const bool integerDivision = op == "%" || (op == "/" && !isReal(*left) && !isReal(*right));
        if (integerDivision && getInteger(*right) == 0) {
            report(base::makeWarning(location, std::format("'{}' by zero, reading it as 0", op)));
        }
        const Value result = applyOperator(op, *left, *right);
        const bool sameSymbol = op == "-" && left->symbol == right->symbol;
        if (!sameSymbol) {
            checkRelocation(left->symbol.empty() ? *right : *left, result);
        }
        left = result;
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
        return std::unexpected(
            base::makeError(locateHere(), "expected ':' in a conditional expression"));
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
    if (value && !isAtEnd()) {
        return std::unexpected(base::makeError(
            locateHere(), std::format("unexpected '{}' after the expression", peek().text)));
    }
    return value;
}

} // namespace

std::expected<Value, base::Diagnostic> evaluate(std::span<const Token> tokens,
                                                const EvalContext& context,
                                                std::vector<base::Diagnostic>& diagnostics) {
    if (context.macros == nullptr) {
        return Evaluator(tokens, context, diagnostics).run();
    }
    Spellings spellings;
    const ExpandOptions options{
        .macros = context.macros,
        .keep =
            [&context](std::string_view name) {
                return findBuiltin(getDialect(context), name) != nullptr ||
                       (context.keep && context.keep(name));
            },
        .spellings = &spellings,
    };
    const std::vector<Token> expanded = expand(tokens, options, diagnostics);
    return Evaluator(expanded, context, diagnostics).run();
}

} // namespace cparser
