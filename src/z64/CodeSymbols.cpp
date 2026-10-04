#include "CodeSymbols.h"

#include "z64/Types.h"

#include <array>
#include <span>

namespace z64 {

namespace {

constexpr usize MatrixSize = 64;

constexpr std::array<u8, MatrixSize> IdentityMatrix = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

constexpr std::array CodeSymbols = {
    CodeSymbol{.name = "gIdentityMtx", .elementSize = MatrixSize, .bytes = IdentityMatrix},
};

} // namespace

std::span<const CodeSymbol> getCodeSymbols() {
    return CodeSymbols;
}

} // namespace z64
