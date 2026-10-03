#include "FixedMatrix.h"

#include "n64/BigEndian.h"
#include "n64/Types.h"

#include <span>

namespace n64 {

namespace {

constexpr usize Dimension = 4;
constexpr usize FractionOffset = 32;
constexpr f32 FixedOne = 65536.0F;

void setHalf(FixedMatrix& matrix, const usize index, const bool fraction, const u16 half) {
    const auto element = static_cast<u32>(matrix.elements[index]);
    const u32 updated =
        fraction ? (element & 0xFFFF'0000U) | half : (element & 0x0000'FFFFU) | (u32{half} << 16);
    matrix.elements[index] = static_cast<s32>(updated);
}

} // namespace

s32 FixedMatrix::getElement(const usize row, const usize column) const {
    return elements[(row * Dimension) + column];
}

f32 FixedMatrix::getFloat(const usize row, const usize column) const {
    return static_cast<f32>(getElement(row, column)) / FixedOne;
}

FixedMatrix makeIdentityMatrix() {
    FixedMatrix matrix;
    for (usize i = 0; i < Dimension; ++i) {
        matrix.elements[(i * Dimension) + i] = 0x0001'0000;
    }
    return matrix;
}

FixedMatrix readFixedMatrix(const std::span<const u8> bytes) {
    FixedMatrix matrix;
    for (usize i = 0; i < matrix.elements.size(); ++i) {
        const u32 integer = readU16(bytes, i * 2);
        const u32 fraction = readU16(bytes, FractionOffset + (i * 2));
        matrix.elements[i] = static_cast<s32>((integer << 16) | fraction);
    }
    return matrix;
}

FixedMatrix multiply(const FixedMatrix& first, const FixedMatrix& second) {
    FixedMatrix product;
    for (usize row = 0; row < Dimension; ++row) {
        for (usize column = 0; column < Dimension; ++column) {
            s64 sum = 0;
            for (usize k = 0; k < Dimension; ++k) {
                sum += s64{first.getElement(row, k)} * second.getElement(k, column);
            }
            product.elements[(row * Dimension) + column] = static_cast<s32>(sum >> 16);
        }
    }
    return product;
}

FixedMatrix insertMatrixWord(const FixedMatrix& matrix, const u32 offset, const u32 word) {
    FixedMatrix updated = matrix;
    const bool fraction = offset >= FractionOffset;
    const usize index = ((offset % FractionOffset) / 2) & ~usize{1};
    setHalf(updated, index, fraction, static_cast<u16>(word >> 16));
    setHalf(updated, index + 1, fraction, static_cast<u16>(word));
    return updated;
}

} // namespace n64
