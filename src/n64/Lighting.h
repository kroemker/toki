#pragma once

#include "n64/FixedMatrix.h"
#include "n64/Types.h"

#include <array>
#include <span>

namespace n64 {

/// A `Light` as F3DEX2 holds it: a colour and a direction in the space the
/// modelview matrix transforms into. The ambient light is a `Light` too, of
/// which only the colour is read.
struct DirectionalLight {
    std::array<u8, 3> color{};
    std::array<s8, 3> direction{};
};

inline constexpr usize LightSize = 24;

/// Reads a `Light` from the 24 bytes F3DEX2 keeps for each one: colour,
/// a copy of the colour, direction, each in four bytes.
[[nodiscard]] DirectionalLight readLight(std::span<const u8, LightSize> bytes);

/// The colour F3DEX2 gives a lit vertex. Each light's direction is taken into
/// model space through the modelview matrix and normalised; the vertex normal
/// is not. Each light adds its colour scaled by the clamped cosine, and the
/// sum is clamped to 255.
[[nodiscard]] std::array<u8, 3> computeLighting(std::array<s8, 3> normal,
                                                std::span<const DirectionalLight> lights,
                                                std::array<u8, 3> ambient,
                                                const FixedMatrix& modelview);

struct TextureGenInput {
    std::array<s8, 3> normal{};
    std::array<s8, 3> lookAtX{};
    std::array<s8, 3> lookAtY{};
    std::array<u16, 2> scale{};
    bool linear = false;
};

/// Texture coordinates, in texels, from `G_TEXTURE_GEN`: the normal against
/// the two look-at directions, either mapped linearly or through `acos` for
/// `G_TEXTURE_GEN_LINEAR`, then scaled by `G_TEXTURE`.
[[nodiscard]] std::array<f32, 2> computeTextureGen(const TextureGenInput& input,
                                                   const FixedMatrix& modelview);

} // namespace n64
