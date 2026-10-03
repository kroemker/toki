#include "Viewport.h"

#include "n64/BigEndian.h"
#include "n64/Types.h"

#include <span>

namespace n64 {

namespace {

constexpr f32 QuarterPixels = 4.0F;
constexpr usize TranslateOffset = 8;

} // namespace

Viewport readViewport(const std::span<const u8> bytes) {
    Viewport viewport;
    for (usize axis = 0; axis < 3; ++axis) {
        const f32 divisor = axis < 2 ? QuarterPixels : 1.0F;
        viewport.scale[axis] = static_cast<f32>(readS16(bytes, axis * 2)) / divisor;
        viewport.translate[axis] =
            static_cast<f32>(readS16(bytes, TranslateOffset + (axis * 2))) / divisor;
    }
    return viewport;
}

Viewport makeDefaultViewport() {
    return Viewport{
        .scale = {160.0F, 120.0F, 511.0F},
        .translate = {160.0F, 120.0F, 511.0F},
    };
}

} // namespace n64
