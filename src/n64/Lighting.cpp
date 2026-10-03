#include "Lighting.h"

#include "n64/FixedMatrix.h"
#include "n64/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

namespace n64 {

namespace {

using Vector = std::array<f32, 3>;

constexpr f32 NormalScale = 127.0F;
constexpr f32 ColorMax = 255.0F;
constexpr usize DirectionOffset = 8;

Vector transformIntoModelSpace(const std::array<s8, 3>& direction, const FixedMatrix& modelview) {
    Vector result{};
    for (usize row = 0; row < 3; ++row) {
        for (usize column = 0; column < 3; ++column) {
            result[row] += modelview.getFloat(row, column) * static_cast<f32>(direction[column]);
        }
    }
    const f32 length =
        std::sqrt((result[0] * result[0]) + (result[1] * result[1]) + (result[2] * result[2]));
    if (length > 0.0F) {
        for (f32& component : result) {
            component /= length;
        }
    }
    return result;
}

f32 dot(const std::array<s8, 3>& normal, const Vector& direction) {
    f32 sum = 0.0F;
    for (usize i = 0; i < 3; ++i) {
        sum += static_cast<f32>(normal[i]) / NormalScale * direction[i];
    }
    return sum;
}

f32 mapTextureGen(const f32 cosine, const bool linear) {
    const f32 clamped = std::clamp(cosine, -1.0F, 1.0F);
    if (linear) {
        return std::acos(-clamped) * (1024.0F / std::numbers::pi_v<f32>);
    }
    return (clamped + 1.0F) * 512.0F;
}

} // namespace

DirectionalLight readLight(const std::span<const u8, LightSize> bytes) {
    DirectionalLight light;
    for (usize i = 0; i < 3; ++i) {
        light.color[i] = bytes[i];
        light.direction[i] = static_cast<s8>(bytes[DirectionOffset + i]);
    }
    return light;
}

std::array<u8, 3> computeLighting(const std::array<s8, 3> normal,
                                  const std::span<const DirectionalLight> lights,
                                  const std::array<u8, 3> ambient, const FixedMatrix& modelview) {
    std::array<f32, 3> sum{};
    for (usize i = 0; i < 3; ++i) {
        sum[i] = ambient[i];
    }
    for (const DirectionalLight& light : lights) {
        const f32 intensity =
            std::max(dot(normal, transformIntoModelSpace(light.direction, modelview)), 0.0F);
        for (usize i = 0; i < 3; ++i) {
            sum[i] += intensity * static_cast<f32>(light.color[i]);
        }
    }
    std::array<u8, 3> color{};
    for (usize i = 0; i < 3; ++i) {
        color[i] = static_cast<u8>(std::min(sum[i], ColorMax));
    }
    return color;
}

std::array<f32, 2> computeTextureGen(const TextureGenInput& input, const FixedMatrix& modelview) {
    const f32 s = dot(input.normal, transformIntoModelSpace(input.lookAtX, modelview));
    const f32 t = dot(input.normal, transformIntoModelSpace(input.lookAtY, modelview));
    return {
        static_cast<f32>(input.scale[0]) / 65536.0F * mapTextureGen(s, input.linear),
        static_cast<f32>(input.scale[1]) / 65536.0F * mapTextureGen(t, input.linear),
    };
}

} // namespace n64
