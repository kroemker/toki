#include "CombineMode.h"

#include "n64/Types.h"

#include <array>

namespace n64 {

namespace {

using enum CombinerInput;

constexpr std::array<CombinerInput, 9> ColorA{
    Combined, Texel0, Texel1, Primitive, Shade, Environment, One, Noise, Zero,
};

constexpr std::array<CombinerInput, 9> ColorB{
    Combined, Texel0, Texel1, Primitive, Shade, Environment, KeyCenter, ConvertK4, Zero,
};

constexpr std::array<CombinerInput, 17> ColorC{
    Combined,       Texel0,     Texel1,           Primitive,   Shade,
    Environment,    KeyScale,   CombinedAlpha,    Texel0Alpha, Texel1Alpha,
    PrimitiveAlpha, ShadeAlpha, EnvironmentAlpha, LodFraction, PrimitiveLodFraction,
    ConvertK5,      Zero,
};

constexpr std::array<CombinerInput, 8> ColorD{
    Combined, Texel0, Texel1, Primitive, Shade, Environment, One, Zero,
};

constexpr std::array<CombinerInput, 8> AlphaAbd{
    CombinedAlpha, Texel0Alpha,      Texel1Alpha, PrimitiveAlpha,
    ShadeAlpha,    EnvironmentAlpha, One,         Zero,
};

constexpr std::array<CombinerInput, 8> AlphaC{
    LodFraction, Texel0Alpha,      Texel1Alpha,          PrimitiveAlpha,
    ShadeAlpha,  EnvironmentAlpha, PrimitiveLodFraction, Zero,
};

template <usize Size>
CombinerInput pick(const std::array<CombinerInput, Size>& table, const u32 selector) {
    return table[selector < Size ? selector : Size - 1];
}

struct CycleSelectors {
    u32 colorA = 0;
    u32 colorB = 0;
    u32 colorC = 0;
    u32 colorD = 0;
    u32 alphaA = 0;
    u32 alphaB = 0;
    u32 alphaC = 0;
    u32 alphaD = 0;
};

CombinerCycle makeCycle(const CycleSelectors& selectors) {
    return CombinerCycle{
        .color = {pick(ColorA, selectors.colorA), pick(ColorB, selectors.colorB),
                  pick(ColorC, selectors.colorC), pick(ColorD, selectors.colorD)},
        .alpha = {pick(AlphaAbd, selectors.alphaA), pick(AlphaAbd, selectors.alphaB),
                  pick(AlphaC, selectors.alphaC), pick(AlphaAbd, selectors.alphaD)},
    };
}

} // namespace

CombineMode makeCombineMode(const u32 word0, const u32 word1) {
    return CombineMode{.cycles = {
                           makeCycle({
                               .colorA = (word0 >> 20) & 0x0F,
                               .colorB = (word1 >> 28) & 0x0F,
                               .colorC = (word0 >> 15) & 0x1F,
                               .colorD = (word1 >> 15) & 0x07,
                               .alphaA = (word0 >> 12) & 0x07,
                               .alphaB = (word1 >> 12) & 0x07,
                               .alphaC = (word0 >> 9) & 0x07,
                               .alphaD = (word1 >> 9) & 0x07,
                           }),
                           makeCycle({
                               .colorA = (word0 >> 5) & 0x0F,
                               .colorB = (word1 >> 24) & 0x0F,
                               .colorC = word0 & 0x1F,
                               .colorD = (word1 >> 6) & 0x07,
                               .alphaA = (word1 >> 21) & 0x07,
                               .alphaB = (word1 >> 3) & 0x07,
                               .alphaC = (word1 >> 18) & 0x07,
                               .alphaD = word1 & 0x07,
                           }),
                       }};
}

} // namespace n64
