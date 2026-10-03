#pragma once

#include "n64/Types.h"

#include <array>
#include <span>

namespace n64 {

/// A 4x4 matrix as the RSP holds it: s15.16 fixed point, row-major, applied to
/// row vectors (`v * M`), so the bottom row is the translation.
struct FixedMatrix {
    std::array<s32, 16> elements{};

    [[nodiscard]] s32 getElement(usize row, usize column) const;

    [[nodiscard]] f32 getFloat(usize row, usize column) const;

    friend bool operator==(const FixedMatrix&, const FixedMatrix&) = default;
};

inline constexpr usize FixedMatrixSize = 64;

[[nodiscard]] FixedMatrix makeIdentityMatrix();

/// Reads an `Mtx`: sixteen big-endian integer halves, then sixteen fraction
/// halves. `bytes` must hold `FixedMatrixSize` bytes.
[[nodiscard]] FixedMatrix readFixedMatrix(std::span<const u8> bytes);

/// `first * second`: a vector transformed by the result is transformed by
/// `first`, then by `second`. Products are summed exactly and shifted back to
/// s15.16, rounding toward negative infinity. The RSP drops the low bits of
/// the fraction-by-fraction products instead, which can differ by one unit in
/// the last place.
[[nodiscard]] FixedMatrix multiply(const FixedMatrix& first, const FixedMatrix& second);

/// Overwrites two adjacent halves of the matrix as `G_MW_MATRIX` does: `offset`
/// is a byte offset into the `Mtx` layout, where the first 32 bytes are the
/// integer halves and the next 32 the fraction halves.
[[nodiscard]] FixedMatrix insertMatrixWord(const FixedMatrix& matrix, u32 offset, u32 word);

} // namespace n64
