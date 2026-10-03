#include "Value.h"

#include "z64/Token.h"
#include "z64/Types.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

namespace z64 {

namespace {

constexpr std::array CastTypes = {
    std::string_view("u8"),       std::string_view("u16"),    std::string_view("u32"),
    std::string_view("u64"),      std::string_view("s8"),     std::string_view("s16"),
    std::string_view("s32"),      std::string_view("s64"),    std::string_view("f32"),
    std::string_view("f64"),      std::string_view("char"),   std::string_view("short"),
    std::string_view("int"),      std::string_view("long"),   std::string_view("float"),
    std::string_view("double"),   std::string_view("void"),   std::string_view("unsigned"),
    std::string_view("signed"),   std::string_view("size_t"), std::string_view("uintptr_t"),
    std::string_view("intptr_t"), std::string_view("u8_t"),
};

[[nodiscard]] s64 truncateTo(std::string_view type, s64 value) {
    if (type == "u8") {
        return static_cast<u8>(value);
    }
    if (type == "char") {
        return static_cast<u8>(value);
    }
    if (type == "s8") {
        return static_cast<s8>(value);
    }
    if (type == "u16") {
        return static_cast<u16>(value);
    }
    if (type == "s16" || type == "short") {
        return static_cast<s16>(value);
    }
    if (type == "u32" || type == "unsigned") {
        return static_cast<u32>(value);
    }
    if (type == "s32" || type == "int" || type == "signed" || type == "long") {
        return static_cast<s32>(value);
    }
    return value;
}

[[nodiscard]] std::optional<Value> integerLiteral(std::string_view text) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        base = 16;
        text.remove_prefix(2);
    } else if (text.size() > 1 && text.front() == '0') {
        base = 8;
        text.remove_prefix(1);
    }

    usize digits = 0;
    while (digits < text.size()) {
        const char c = text[digits];
        const bool valid = (c >= '0' && c <= '9') ||
                           (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        if (!valid) {
            break;
        }
        ++digits;
    }

    u64 parsed = 0;
    const char* begin = text.data();
    const auto [stop, failure] = std::from_chars(begin, begin + digits, parsed, base);
    if (failure != std::errc() || stop != begin + digits) {
        return std::nullopt;
    }

    const std::string_view suffix = text.substr(digits);
    for (const char c : suffix) {
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L') {
            return std::nullopt;
        }
    }
    return integerValue(static_cast<s64>(parsed));
}

[[nodiscard]] std::optional<Value> realLiteral(std::string_view text) {
    bool single = false;
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
        single = true;
        text.remove_suffix(1);
    } else if (!text.empty() && (text.back() == 'l' || text.back() == 'L')) {
        text.remove_suffix(1);
    }
    f64 parsed = 0.0;
    const char* begin = text.data();
    const auto [stop, failure] = std::from_chars(begin, begin + text.size(), parsed);
    if (failure != std::errc() || stop != begin + text.size()) {
        return std::nullopt;
    }
    return single ? floatValue(parsed) : doubleValue(parsed);
}

[[nodiscard]] std::optional<Value> characterLiteral(std::string_view text) {
    if (text.size() < 3 || text.front() != '\'' || text.back() != '\'') {
        return std::nullopt;
    }
    const std::string_view body = text.substr(1, text.size() - 2);
    if (body.size() == 1) {
        return integerValue(static_cast<u8>(body[0]));
    }
    if (body.size() != 2 || body[0] != '\\') {
        return std::nullopt;
    }
    switch (body[1]) {
    case '0':
        return integerValue(0);
    case 'n':
        return integerValue('\n');
    case 't':
        return integerValue('\t');
    case 'r':
        return integerValue('\r');
    case '\\':
        return integerValue('\\');
    case '\'':
        return integerValue('\'');
    case '"':
        return integerValue('"');
    default:
        return std::nullopt;
    }
}

[[nodiscard]] Value compare(std::string_view op, bool less, bool equal) {
    if (op == "<") {
        return integerValue(less ? 1 : 0);
    }
    if (op == ">") {
        return integerValue(!less && !equal ? 1 : 0);
    }
    if (op == "<=") {
        return integerValue(less || equal ? 1 : 0);
    }
    if (op == ">=") {
        return integerValue(!less ? 1 : 0);
    }
    if (op == "==") {
        return integerValue(equal ? 1 : 0);
    }
    return integerValue(equal ? 0 : 1);
}

[[nodiscard]] f64 divide(f64 a, f64 b) {
    if (b != 0.0) {
        return a / b;
    }
    if (a == 0.0 || std::isnan(a)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    const bool negative = std::signbit(a) != std::signbit(b);
    return negative ? -std::numeric_limits<f64>::infinity() : std::numeric_limits<f64>::infinity();
}

[[nodiscard]] bool isArithmetic(std::string_view op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "<" || op == ">" ||
           op == "<=" || op == ">=" || op == "==" || op == "!=";
}

[[nodiscard]] Value applyReal(std::string_view op, f64 a, f64 b, ValueType type) {
    const auto make = [type](f64 value) {
        return type == ValueType::Float ? floatValue(value) : doubleValue(value);
    };
    if (op == "+") {
        return make(a + b);
    }
    if (op == "-") {
        return make(a - b);
    }
    if (op == "*") {
        return make(a * b);
    }
    if (op == "/") {
        return make(divide(a, b));
    }
    return compare(op, a < b, a == b);
}

[[nodiscard]] Value applyInteger(std::string_view op, s64 a, s64 b) {
    const u64 wide = static_cast<u64>(a);
    if (op == "+") {
        return integerValue(static_cast<s64>(wide + static_cast<u64>(b)));
    }
    if (op == "-") {
        return integerValue(static_cast<s64>(wide - static_cast<u64>(b)));
    }
    if (op == "*") {
        return integerValue(static_cast<s64>(wide * static_cast<u64>(b)));
    }
    if (op == "/" || op == "%") {
        if (b == 0) {
            return integerValue(0);
        }
        if (b == -1) {
            return integerValue(op == "/" ? static_cast<s64>(u64{0} - wide) : 0);
        }
        return integerValue(op == "/" ? a / b : a % b);
    }
    if (op == "<<") {
        return integerValue(static_cast<s64>(wide << (static_cast<u64>(b) & 63)));
    }
    if (op == ">>") {
        return integerValue(a >> (static_cast<u64>(b) & 63));
    }
    if (op == "&") {
        return integerValue(a & b);
    }
    if (op == "|") {
        return integerValue(a | b);
    }
    if (op == "^") {
        return integerValue(a ^ b);
    }
    return compare(op, a < b, a == b);
}

[[nodiscard]] Value apply(std::string_view op, const Value& left, const Value& right) {
    if (op == "&&") {
        return integerValue(isTrue(left) && isTrue(right) ? 1 : 0);
    }
    if (op == "||") {
        return integerValue(isTrue(left) || isTrue(right) ? 1 : 0);
    }
    if ((isReal(left) || isReal(right)) && isArithmetic(op)) {
        if (left.type == ValueType::Double || right.type == ValueType::Double) {
            return applyReal(op, asReal(left), asReal(right), ValueType::Double);
        }
        return applyReal(op, floatValue(asReal(left)).number, floatValue(asReal(right)).number,
                         ValueType::Float);
    }
    return applyInteger(op, asInteger(left), asInteger(right));
}
} // namespace

Value integerValue(s64 value) {
    return Value{.type = ValueType::Integer, .integer = value, .number = static_cast<f64>(value)};
}

Value floatValue(f64 value) {
    constexpr f64 Largest = std::numeric_limits<f32>::max();
    constexpr f64 Overflow = Largest + 0x1p103;
    const f64 magnitude = std::fabs(value);
    if (magnitude >= Overflow) {
        return Value{.type = ValueType::Float,
                     .number = std::copysign(std::numeric_limits<f64>::infinity(), value)};
    }
    if (magnitude > Largest) {
        return Value{.type = ValueType::Float, .number = std::copysign(Largest, value)};
    }
    return Value{.type = ValueType::Float, .number = static_cast<f64>(static_cast<f32>(value))};
}

Value doubleValue(f64 value) {
    return Value{.type = ValueType::Double, .number = value};
}

bool isReal(const Value& value) {
    return value.type != ValueType::Integer;
}

bool isTrue(const Value& value) {
    return isReal(value) ? value.number != 0.0 : value.integer != 0;
}

s64 asInteger(const Value& value) {
    if (!isReal(value)) {
        return value.integer;
    }
    constexpr f64 Limit = 0x1p63;
    if (std::isnan(value.number)) {
        return 0;
    }
    if (value.number >= Limit) {
        return std::numeric_limits<s64>::max();
    }
    if (value.number < -Limit) {
        return std::numeric_limits<s64>::min();
    }
    return static_cast<s64>(std::trunc(value.number));
}

f64 asReal(const Value& value) {
    return isReal(value) ? value.number : static_cast<f64>(value.integer);
}

std::optional<Value> literalValue(const Token& token) {
    switch (token.kind) {
    case TokenKind::Integer:
        return integerLiteral(token.text);
    case TokenKind::Real:
        return realLiteral(token.text);
    case TokenKind::Character:
        return characterLiteral(token.text);
    default:
        return std::nullopt;
    }
}

Value applyOperator(std::string_view op, const Value& left, const Value& right) {
    return apply(op, left, right);
}

bool isCastType(std::string_view name) {
    return std::ranges::find(CastTypes, name) != CastTypes.end();
}

s64 sizeOfType(std::string_view name) {
    if (name == "u8" || name == "s8" || name == "char") {
        return 1;
    }
    if (name == "u16" || name == "s16" || name == "short") {
        return 2;
    }
    if (name == "u64" || name == "s64" || name == "f64" || name == "double") {
        return 8;
    }
    return 4;
}

Value castTo(std::string_view type, bool pointer, const Value& operand) {
    if (pointer) {
        return integerValue(asInteger(operand));
    }
    if (type == "f32" || type == "float") {
        return floatValue(asReal(operand));
    }
    if (type == "f64" || type == "double") {
        return doubleValue(asReal(operand));
    }
    return integerValue(truncateTo(type, asInteger(operand)));
}

} // namespace z64
