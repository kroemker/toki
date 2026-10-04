#pragma once

#include "cparser/Types.h"

#include <optional>
#include <string_view>

namespace cparser {

struct ScalarType;
struct Token;

enum class ValueType : u8 {
    Integer,
    Float,
    Double,
};

/// A constant expression's result. Decomp data is overwhelmingly integer, but
/// fast64 writes rotations as `DEG_TO_BINANG(0.000)` and vertex scales as
/// floats, so both have to survive evaluation, and `float` has to stay
/// `float`: `0x8000 / 180.0f` is computed in single precision, and computing
/// it in double moves some angles by one binary angle unit.
///
/// A `Float` is held in `number` already rounded to single precision.
struct Value {
    ValueType type = ValueType::Integer;
    s64 integer = 0;
    f64 number = 0.0;

    /// For a pointer, the size of what it points at, so adding an integer
    /// moves it by whole elements as C does. Zero for anything else.
    usize stride = 0;
};

[[nodiscard]] Value makeInteger(s64 value);

/// Rounds to single precision, as a `float` would hold it.
[[nodiscard]] Value makeFloat(f64 value);

[[nodiscard]] Value makeDouble(f64 value);

[[nodiscard]] bool isReal(const Value& value);

/// Whether the value counts as true in a condition: nonzero, for a real too.
[[nodiscard]] bool isTrue(const Value& value);

/// Truncates toward zero, the way C converts a real to an integer. C leaves a
/// real outside the integer's range undefined; this saturates, and reads NaN
/// as zero.
[[nodiscard]] s64 getInteger(const Value& value);

[[nodiscard]] f64 getReal(const Value& value);

/// The value of an integer, floating point or character literal as the decomp
/// spells it, including the `0x`, `0`, `u`, `L` and `f` forms.
[[nodiscard]] std::optional<Value> parseLiteral(const Token& token);

/// One binary C operator. Anything the parser did not recognise as an operator
/// never reaches here, so an unknown spelling is treated as `!=`. A bitwise
/// operator or shift on a real, which C rejects, truncates it to an integer.
/// Integer division by zero is 0 here; the evaluator is what reports it.
/// Adding to or subtracting from a pointer scales the integer by its stride,
/// and the difference of two pointers counts elements.
[[nodiscard]] Value applyOperator(std::string_view op, const Value& left, const Value& right);

/// Applies a cast the way `type` converts: an integer type wraps to its size
/// and signedness, a 4-byte real rounds to single precision, and a pointer
/// cast leaves the bits alone and points at `type`, or at a pointer when
/// `pointerDepth` is more than one.
[[nodiscard]] Value castTo(const ScalarType& type, usize pointerDepth, const Value& operand);

} // namespace cparser
