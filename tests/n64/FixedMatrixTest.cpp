#include "n64/FixedMatrix.h"

#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using base::s32;
using base::u8;

namespace {

constexpr s32 One = 0x0001'0000;

n64::FixedMatrix makeMatrix(const std::array<s32, 16>& elements) {
    return n64::FixedMatrix{.elements = elements};
}

} // namespace

TEST_CASE("an Mtx keeps its integer halves before its fraction halves", "[n64][matrix]") {
    std::array<u8, n64::FixedMatrixSize> bytes{};
    bytes[0] = 0xFF;
    bytes[1] = 0xFE;
    bytes[32] = 0x80;
    bytes[33] = 0x00;

    const n64::FixedMatrix matrix = n64::readFixedMatrix(bytes);

    REQUIRE(matrix.getElement(0, 0) == -0x0001'8000);
    REQUIRE(matrix.getFloat(0, 0) == -1.5F);
}

TEST_CASE("multiplying by the identity changes nothing", "[n64][matrix]") {
    const n64::FixedMatrix matrix =
        makeMatrix({One, 2, 3, 4, 5, One, 7, 8, 9, 10, One, 12, -One, 3 * One, 15, One});

    REQUIRE(n64::multiply(matrix, n64::makeIdentityMatrix()) == matrix);
    REQUIRE(n64::multiply(n64::makeIdentityMatrix(), matrix) == matrix);
}

TEST_CASE("fixed point products round toward negative infinity", "[n64][matrix]") {
    std::array<s32, 16> half{};
    half[0] = One / 2;
    std::array<s32, 16> small{};
    small[0] = -1;

    REQUIRE(n64::multiply(makeMatrix(half), makeMatrix(small)).getElement(0, 0) == -1);
}

TEST_CASE("an inserted word replaces two adjacent halves", "[n64][matrix]") {
    const n64::FixedMatrix identity = n64::makeIdentityMatrix();

    const n64::FixedMatrix integers = n64::insertMatrixWord(identity, 0x08, 0x0002'0003);
    const n64::FixedMatrix fractions = n64::insertMatrixWord(identity, 0x20, 0x8000'4000);

    REQUIRE(integers.getElement(1, 0) == 2 * One);
    REQUIRE(integers.getElement(1, 1) == 3 * One);
    REQUIRE(fractions.getElement(0, 0) == One + 0x8000);
    REQUIRE(fractions.getElement(0, 1) == 0x4000);
}
