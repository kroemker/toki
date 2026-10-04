#pragma once

#include "n64/Types.h"

#include <array>
#include <span>

namespace n64 {

/// A `Vp` decoded: x and y in pixels, z in the raw depth units of `G_MAXZ`.
/// A position maps to the screen as `ndc * scale + translate`, except that y
/// is negated first, because screen y grows downward.
struct Viewport {
    std::array<f32, 3> scale{};
    std::array<f32, 3> translate{};

    friend bool operator==(const Viewport&, const Viewport&) = default;
};

inline constexpr usize ViewportSize = 16;

/// Reads a `Vp`: four big-endian scale halves, then four translate halves, x
/// and y in quarter pixels. `bytes` must hold `ViewportSize` bytes.
[[nodiscard]] Viewport readViewport(std::span<const u8> bytes);

/// Writes a `Vp` in the layout `readViewport` reads, rounding each value to
/// the nearest unit it is stored in.
[[nodiscard]] std::array<u8, ViewportSize> writeViewport(const Viewport& viewport);

/// The viewport F3DEX2 starts a task with: a 320 by 240 screen.
[[nodiscard]] Viewport makeDefaultViewport();

} // namespace n64
