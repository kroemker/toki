#include "TypeParser.h"

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "cparser/Dialect.h"
#include "cparser/Expression.h"
#include "cparser/Preprocessor.h"
#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/TypeName.h"
#include "cparser/TypeTable.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cparser {

namespace {

constexpr std::array<std::string_view, 13> Ignored{
    "const",    "volatile",   "static",   "extern",     "inline",        "register", "auto",
    "restrict", "__restrict", "__inline", "__inline__", "__extension__", "_Noreturn"};

constexpr std::array<std::string_view, 10> ScalarWords{
    "unsigned", "char", "short", "int", "long", "float", "double", "void", "_Bool", "signed"};

bool contains(const std::span<const std::string_view> words, const std::string_view word) {
    return std::ranges::find(words, word) != words.end();
}

bool isAttribute(const Token& token) {
    return isIdentifier(token, "__attribute__") || isIdentifier(token, "__attribute");
}

struct Specifier {
    const CType* type = nullptr;
    bool isTypedef = false;
};

struct Suffix {
    bool isFunction = false;
    std::optional<usize> count;
};

struct Shape {
    usize pointers = 0;
    std::unique_ptr<Shape> inner;
    std::string_view name;
    std::vector<Suffix> suffixes;
};

struct Declarator {
    std::string_view name;
    const CType* type = nullptr;
};

class TypeReader {
public:
    TypeReader(std::span<const Token> tokens, const EvalContext& context, TypeParseResult& result)
        : tokens(tokens), context(context), result(result) {}

    void run();

private:
    [[nodiscard]] bool isAtEnd() const {
        return cursor >= tokens.size() || tokens[cursor].kind == TokenKind::EndOfFile;
    }

    [[nodiscard]] const Token& peek(const usize ahead = 0) const {
        return cursor + ahead < tokens.size() ? tokens[cursor + ahead] : tokens.back();
    }

    [[nodiscard]] bool isAt(const std::string_view punctuator, const usize ahead = 0) const {
        return cursor + ahead < tokens.size() && isPunctuator(tokens[cursor + ahead], punctuator);
    }

    [[nodiscard]] std::optional<Specifier> readSpecifier();
    [[nodiscard]] const CType* readRecord();
    [[nodiscard]] const CType* readEnum();
    [[nodiscard]] std::vector<Field> readFields();
    [[nodiscard]] Shape readShape();
    [[nodiscard]] Declarator applyShape(const CType* base, const Shape& shape);
    [[nodiscard]] bool isNestedDeclarator() const;
    [[nodiscard]] usize readAttributes();
    [[nodiscard]] std::optional<s64> evaluateUntil(std::span<const std::string_view> stops);
    [[nodiscard]] usize findTopLevel(std::span<const std::string_view> stops) const;
    void skipBalanced();
    void skipStatement();
    void readStatement();

    void report(std::string message) {
        result.diagnostics.push_back(base::makeWarning(locate(peek()), std::move(message)));
    }

    std::span<const Token> tokens;
    const EvalContext& context;
    TypeParseResult& result;
    usize cursor = 0;
};

usize TypeReader::findTopLevel(const std::span<const std::string_view> stops) const {
    int nesting = 0;
    for (usize at = cursor; at < tokens.size(); ++at) {
        const Token& token = tokens[at];
        if (token.kind == TokenKind::EndOfFile) {
            return at;
        }
        if (nesting == 0 && std::ranges::any_of(stops, [&](const std::string_view stop) {
                return isPunctuator(token, stop);
            })) {
            return at;
        }
        if (isPunctuator(token, "(") || isPunctuator(token, "[") || isPunctuator(token, "{")) {
            ++nesting;
        } else if (isPunctuator(token, ")") || isPunctuator(token, "]") ||
                   isPunctuator(token, "}")) {
            if (nesting == 0) {
                return at;
            }
            --nesting;
        }
    }
    return tokens.size();
}

void TypeReader::skipBalanced() {
    int nesting = 0;
    do {
        const Token& token = peek();
        if (isPunctuator(token, "(") || isPunctuator(token, "[") || isPunctuator(token, "{")) {
            ++nesting;
        } else if (isPunctuator(token, ")") || isPunctuator(token, "]") ||
                   isPunctuator(token, "}")) {
            --nesting;
        }
        ++cursor;
    } while (!isAtEnd() && nesting > 0);
}

void TypeReader::skipStatement() {
    while (!isAtEnd()) {
        if (isAt(";")) {
            ++cursor;
            return;
        }
        if (isAt("{")) {
            const bool isBody = cursor > 0 && isPunctuator(tokens[cursor - 1], ")");
            skipBalanced();
            if (isBody) {
                return;
            }
            continue;
        }
        if (isAt("(") || isAt("[")) {
            skipBalanced();
            continue;
        }
        ++cursor;
    }
}

std::optional<s64> TypeReader::evaluateUntil(const std::span<const std::string_view> stops) {
    const usize end = findTopLevel(stops);
    const std::span<const Token> expression = tokens.subspan(cursor, end - cursor);
    cursor = end;
    if (expression.empty()) {
        return std::nullopt;
    }
    EvalContext nested = context;
    nested.macros = nullptr;
    nested.unknownIsZero = false;
    nested.findTypeSize = [this](const std::string_view name) -> std::optional<usize> {
        const CType* type = result.types.findTypedef(name);
        if (type == nullptr) {
            type = result.types.findTag(name);
        }
        return type != nullptr && type->complete ? std::optional(type->size) : std::nullopt;
    };
    nested.resolve = [this](const std::string_view name) -> std::optional<Value> {
        if (const std::optional<s64> value = result.types.findEnumerator(name)) {
            return makeInteger(*value);
        }
        return context.resolve ? context.resolve(name) : std::nullopt;
    };
    const std::expected<Value, base::Diagnostic> value =
        evaluate(expression, nested, result.diagnostics);
    if (!value) {
        result.diagnostics.push_back(value.error());
        return std::nullopt;
    }
    return getInteger(*value);
}

usize TypeReader::readAttributes() {
    usize alignment = 0;
    while (!isAtEnd() && isAttribute(peek())) {
        ++cursor;
        const usize open = cursor;
        skipBalanced();
        for (usize at = open; at + 2 < cursor; ++at) {
            if ((isIdentifier(tokens[at], "aligned") || isIdentifier(tokens[at], "__aligned__")) &&
                isPunctuator(tokens[at + 1], "(")) {
                const usize saved = cursor;
                cursor = at + 2;
                static constexpr std::array Close = {std::string_view(")")};
                if (const std::optional<s64> value = evaluateUntil(Close); value && *value > 0) {
                    alignment = std::max(alignment, static_cast<usize>(*value));
                }
                cursor = saved;
            }
        }
    }
    return alignment;
}

std::optional<Specifier> TypeReader::readSpecifier() {
    Specifier specifier;
    std::vector<std::string_view> words;
    while (!isAtEnd() && peek().kind == TokenKind::Identifier) {
        const std::string_view word = peek().text;
        if (word == "typedef") {
            specifier.isTypedef = true;
            ++cursor;
        } else if (isAttribute(peek())) {
            static_cast<void>(readAttributes());
        } else if (contains(Ignored, word)) {
            ++cursor;
        } else if (word == "struct" || word == "union") {
            specifier.type = readRecord();
        } else if (word == "enum") {
            specifier.type = readEnum();
        } else if (specifier.type == nullptr && words.empty() &&
                   result.types.findTypedef(word) != nullptr) {
            specifier.type = result.types.findTypedef(word);
            ++cursor;
        } else if (contains(ScalarWords, word) ||
                   (specifier.type == nullptr && words.empty() && context.dialect != nullptr &&
                    findType(*context.dialect, word) != nullptr)) {
            words.push_back(word);
            ++cursor;
        } else {
            break;
        }
    }
    if (specifier.type == nullptr && !words.empty() && context.dialect != nullptr) {
        if (const std::optional<ScalarType> scalar = combineScalarWords(*context.dialect, words)) {
            specifier.type = result.types.getScalar(*scalar);
        }
    }
    if (specifier.type == nullptr) {
        return std::nullopt;
    }
    return specifier;
}

const CType* TypeReader::readRecord() {
    const TypeKind kind = isIdentifier(peek(), "union") ? TypeKind::Union : TypeKind::Struct;
    ++cursor;
    usize alignment = readAttributes();
    std::string_view name;
    if (peek().kind == TokenKind::Identifier && !isAttribute(peek())) {
        name = peek().text;
        ++cursor;
    }
    alignment = std::max(alignment, readAttributes());
    CType* record = result.types.declareRecord(kind, name);
    if (!isAt("{")) {
        return record;
    }
    ++cursor;
    std::vector<Field> fields = readFields();
    if (isAt("}")) {
        ++cursor;
    }
    alignment = std::max(alignment, readAttributes());
    TypeTable::completeRecord(*record, std::move(fields), alignment);
    return record;
}

std::vector<Field> TypeReader::readFields() {
    static constexpr std::array WidthStops = {std::string_view(","), std::string_view(";")};
    std::vector<Field> fields;
    while (!isAtEnd() && !isAt("}")) {
        const std::optional<Specifier> specifier = readSpecifier();
        if (!specifier) {
            report(std::format("cannot read the member type '{}'", peek().text));
            skipStatement();
            continue;
        }
        if (isAt(";")) {
            fields.push_back(Field{.type = specifier->type});
            ++cursor;
            continue;
        }
        while (!isAtEnd()) {
            Field field{.type = specifier->type};
            if (!isAt(":")) {
                const Declarator declarator = applyShape(specifier->type, readShape());
                field.name = std::string(declarator.name);
                field.type = declarator.type;
            }
            if (isAt(":")) {
                ++cursor;
                field.bitWidth = static_cast<u8>(evaluateUntil(WidthStops).value_or(0));
            }
            static_cast<void>(readAttributes());
            fields.push_back(std::move(field));
            if (isAt(",")) {
                ++cursor;
                continue;
            }
            if (isAt(";")) {
                ++cursor;
            } else {
                report(std::format("cannot read a member at '{}'", peek().text));
                skipStatement();
            }
            break;
        }
    }
    return fields;
}

const CType* TypeReader::readEnum() {
    static constexpr std::array ValueStops = {std::string_view(","), std::string_view("}")};
    ++cursor;
    static_cast<void>(readAttributes());
    if (peek().kind == TokenKind::Identifier) {
        ++cursor;
    }
    if (isAt("{")) {
        ++cursor;
        s64 next = 0;
        while (!isAtEnd() && !isAt("}")) {
            if (peek().kind != TokenKind::Identifier) {
                report(std::format("cannot read an enumerator at '{}'", peek().text));
                cursor = findTopLevel(ValueStops);
            } else {
                const std::string_view name = peek().text;
                ++cursor;
                if (isAt("=")) {
                    ++cursor;
                    next = evaluateUntil(ValueStops).value_or(next);
                }
                result.types.defineEnumerator(name, next);
                ++next;
            }
            if (isAt(",")) {
                ++cursor;
            }
        }
        if (isAt("}")) {
            ++cursor;
        }
    }
    static constexpr ScalarType Int{.name = "int", .size = 4};
    const ScalarType* integer =
        context.dialect != nullptr ? findType(*context.dialect, "int") : nullptr;
    return result.types.getScalar(integer != nullptr ? *integer : Int);
}

bool TypeReader::isNestedDeclarator() const {
    const Token& next = peek(1);
    if (isPunctuator(next, "*") || isPunctuator(next, "(") || isPunctuator(next, "[")) {
        return true;
    }
    return next.kind == TokenKind::Identifier && !contains(ScalarWords, next.text) &&
           !contains(Ignored, next.text) && result.types.findTypedef(next.text) == nullptr &&
           next.text != "struct" && next.text != "union" && next.text != "enum";
}

Shape TypeReader::readShape() {
    static constexpr std::array BoundStops = {std::string_view("]")};
    Shape shape;
    while (!isAtEnd()) {
        if (isAt("*")) {
            ++shape.pointers;
            ++cursor;
        } else if (peek().kind == TokenKind::Identifier && contains(Ignored, peek().text)) {
            ++cursor;
        } else if (isAttribute(peek())) {
            static_cast<void>(readAttributes());
        } else {
            break;
        }
    }
    if (isAt("(") && isNestedDeclarator()) {
        ++cursor;
        shape.inner = std::make_unique<Shape>(readShape());
        if (isAt(")")) {
            ++cursor;
        }
    } else if (peek().kind == TokenKind::Identifier && !isAttribute(peek())) {
        shape.name = peek().text;
        ++cursor;
    }
    while (!isAtEnd()) {
        if (isAt("[")) {
            ++cursor;
            std::optional<usize> count;
            if (const std::optional<s64> bound = evaluateUntil(BoundStops); bound && *bound >= 0) {
                count = static_cast<usize>(*bound);
            }
            if (isAt("]")) {
                ++cursor;
            }
            shape.suffixes.push_back(Suffix{.count = count});
        } else if (isAt("(")) {
            skipBalanced();
            shape.suffixes.push_back(Suffix{.isFunction = true});
        } else {
            break;
        }
    }
    return shape;
}

Declarator TypeReader::applyShape(const CType* base, const Shape& shape) {
    const CType* type = base;
    for (usize i = 0; i < shape.pointers; ++i) {
        type = result.types.getPointer(type);
    }
    for (auto suffix = shape.suffixes.rbegin(); suffix != shape.suffixes.rend(); ++suffix) {
        type = suffix->isFunction ? result.types.getFunction()
                                  : result.types.getArray(type, suffix->count);
    }
    if (shape.inner) {
        return applyShape(type, *shape.inner);
    }
    return Declarator{.name = shape.name, .type = type};
}

void TypeReader::readStatement() {
    static constexpr std::array InitializerStops = {std::string_view(","), std::string_view(";")};
    const std::optional<Specifier> specifier = readSpecifier();
    if (!specifier) {
        skipStatement();
        return;
    }
    while (!isAtEnd()) {
        if (isAt(";")) {
            ++cursor;
            return;
        }
        const Declarator declarator = applyShape(specifier->type, readShape());
        static_cast<void>(readAttributes());
        if (declarator.type->kind == TypeKind::Function && isAt("{")) {
            skipBalanced();
            return;
        }
        if (specifier->isTypedef && !declarator.name.empty()) {
            result.types.defineTypedef(declarator.name, declarator.type);
        }
        if (isAt("=")) {
            ++cursor;
            cursor = findTopLevel(InitializerStops);
        }
        if (isAt(",")) {
            ++cursor;
            continue;
        }
        if (!isAt(";")) {
            skipStatement();
            return;
        }
    }
}

void TypeReader::run() {
    while (!isAtEnd()) {
        if (isAt(";")) {
            ++cursor;
            continue;
        }
        const usize before = cursor;
        readStatement();
        if (cursor == before) {
            ++cursor;
        }
    }
}

} // namespace

TypeParseResult parseTypes(const TranslationUnit& unit, const EvalContext& context) {
    TypeParseResult result;
    if (!unit.tokens.empty()) {
        TypeReader(unit.tokens, context, result).run();
    }
    return result;
}

} // namespace cparser
