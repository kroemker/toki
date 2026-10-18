#include "BinaryAngle.h"

#include <cmath>

namespace toki::scene {

namespace {

constexpr f64 UnitsPerTurn = 65536.0;
constexpr f64 DegreesPerTurn = 360.0;

} // namespace

f32 convertToDegrees(const s16 angle) {
    return static_cast<f32>(angle * DegreesPerTurn / UnitsPerTurn);
}

s16 convertToBinaryAngle(const f32 degrees) {
    const auto units = static_cast<s64>(std::llround(degrees * UnitsPerTurn / DegreesPerTurn));
    return static_cast<s16>(static_cast<u16>(units & 0xFFFF));
}

} // namespace toki::scene
