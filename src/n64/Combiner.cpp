#include "Combiner.h"

#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"
#include "n64/Types.h"

#include <algorithm>
#include <array>
#include <utility>

namespace n64 {

namespace {

using Channels = std::array<s32, 4>;

constexpr s32 FullScale = 0x100;
constexpr usize AlphaChannel = 3;

s32 signExtendNine(const s32 value) {
    return ((value & 0x1FF) ^ 0x100) - 0x100;
}

s32 expandSpecial(const s32 value) {
    return signExtendNine(value - 0x80) + 0x80;
}

s32 clampNine(const s32 value) {
    return std::clamp(signExtendNine(value - 0x80) + 0x80, 0, 0xFF);
}

Channels makeChannels(const Color& color) {
    return {color[0], color[1], color[2], color[3]};
}

struct Sources {
    Channels combined{};
    Channels texel0{};
    Channels texel1{};
};

s32 selectColorInput(const CombinerInput input, const usize channel, const Sources& sources,
                     const CombinerInputs& inputs) {
    const usize rgb = std::min(channel, usize{2});
    switch (input) {
    case CombinerInput::Combined:
        return sources.combined[channel];
    case CombinerInput::Texel0:
        return sources.texel0[channel];
    case CombinerInput::Texel1:
        return sources.texel1[channel];
    case CombinerInput::Primitive:
        return inputs.primitive[channel];
    case CombinerInput::Shade:
        return inputs.shade[channel];
    case CombinerInput::Environment:
        return inputs.environment[channel];
    case CombinerInput::One:
        return FullScale;
    case CombinerInput::Noise:
        return inputs.noise;
    case CombinerInput::KeyCenter:
        return inputs.keyCenter[rgb];
    case CombinerInput::KeyScale:
        return inputs.keyScale[rgb];
    case CombinerInput::ConvertK4:
        return inputs.convertK4;
    case CombinerInput::ConvertK5:
        return inputs.convertK5;
    default:
        return 0;
    }
}

s32 selectInput(const CombinerInput input, const usize channel, const Sources& sources,
                const CombinerInputs& inputs) {
    switch (input) {
    case CombinerInput::CombinedAlpha:
        return sources.combined[AlphaChannel];
    case CombinerInput::Texel0Alpha:
        return sources.texel0[AlphaChannel];
    case CombinerInput::Texel1Alpha:
        return sources.texel1[AlphaChannel];
    case CombinerInput::PrimitiveAlpha:
        return inputs.primitive[AlphaChannel];
    case CombinerInput::ShadeAlpha:
        return inputs.shade[AlphaChannel];
    case CombinerInput::EnvironmentAlpha:
        return inputs.environment[AlphaChannel];
    case CombinerInput::LodFraction:
        return inputs.lodFraction;
    case CombinerInput::PrimitiveLodFraction:
        return inputs.primitiveLodFraction;
    default:
        return selectColorInput(input, channel, sources, inputs);
    }
}

Channels runCycle(const CombinerCycle& cycle, const Sources& sources,
                  const CombinerInputs& inputs) {
    Channels result{};
    for (usize channel = 0; channel < result.size(); ++channel) {
        const auto& slots = channel == AlphaChannel ? cycle.alpha : cycle.color;
        const s32 a = expandSpecial(selectInput(slots[0], channel, sources, inputs));
        const s32 b = expandSpecial(selectInput(slots[1], channel, sources, inputs));
        const s32 c = signExtendNine(selectInput(slots[2], channel, sources, inputs));
        const s32 d = expandSpecial(selectInput(slots[3], channel, sources, inputs));
        result[channel] = ((((a - b) * c) + 0x80) >> 8) + d;
    }
    return result;
}

s32 expandAlpha(const s32 alpha) {
    return alpha + ((alpha + 1) >> 8);
}

s32 modulateByCoverage(const s32 alpha, const s32 coverage, const bool timesAlpha) {
    return timesAlpha ? ((alpha * coverage) + 4) >> 3 : coverage << 5;
}

u8 computeAlphaReference(const OtherMode& otherMode, const s32 alpha, const s32 coverage) {
    s32 expanded = expandAlpha(clampNine(alpha));
    if (otherMode.isAlphaCoverageSelect()) {
        expanded = modulateByCoverage(expanded, coverage, otherMode.isCoverageTimesAlpha());
    }
    return static_cast<u8>(std::clamp(expanded, 0, 0xFF));
}

u8 computeCoverage(const OtherMode& otherMode, const s32 alpha, const s32 coverage) {
    if (!otherMode.isCoverageTimesAlpha()) {
        return static_cast<u8>(coverage);
    }
    return static_cast<u8>(((((alpha * coverage) + 4) >> 3) >> 5) & 0xF);
}

} // namespace

CombinerResult evaluateCombiner(const CombineMode& combineMode, const OtherMode& otherMode,
                                const CombinerInputs& inputs) {
    const bool twoCycle = otherMode.getCycleType() == CycleType::Two;
    Sources sources{
        .combined = {},
        .texel0 = makeChannels(inputs.texel0),
        .texel1 = makeChannels(inputs.texel1),
    };
    CombinerResult result;
    if (twoCycle) {
        sources.combined = runCycle(combineMode.cycles[0], sources, inputs);
        if (otherMode.getAlphaCompare() != AlphaCompare::None) {
            result.alphaReference =
                computeAlphaReference(otherMode, sources.combined[AlphaChannel], inputs.coverage);
        }
        std::swap(sources.texel0, sources.texel1);
    }

    const Channels combined = runCycle(combineMode.cycles[1], sources, inputs);
    for (usize channel = 0; channel < AlphaChannel; ++channel) {
        result.color[channel] = static_cast<u8>(clampNine(combined[channel]));
    }
    s32 alpha = expandAlpha(clampNine(combined[AlphaChannel]));
    result.coverage = computeCoverage(otherMode, alpha, inputs.coverage);
    if (otherMode.isAlphaCoverageSelect()) {
        alpha = modulateByCoverage(alpha, inputs.coverage, otherMode.isCoverageTimesAlpha());
    }
    result.color[AlphaChannel] = static_cast<u8>(std::clamp(alpha, 0, 0xFF));
    if (!twoCycle) {
        result.alphaReference = result.color[AlphaChannel];
    }
    return result;
}

bool passesAlphaCompare(const OtherMode& otherMode, const u8 alphaReference, const u8 threshold) {
    return otherMode.getAlphaCompare() == AlphaCompare::None || alphaReference >= threshold;
}

bool isPixelWritten(const OtherMode& otherMode, const u8 coverage) {
    return !otherMode.isAntiAliased() || coverage != 0;
}

} // namespace n64
