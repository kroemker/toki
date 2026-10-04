#include "Blender.h"

#include "n64/Color.h"
#include "n64/OtherMode.h"
#include "n64/Types.h"

#include <array>

namespace n64 {

namespace {

using Rgb = std::array<u8, 3>;

Rgb pickColor(const BlendColor input, const Color& pixel, const BlenderInputs& inputs) {
    const auto toRgb = [](const Color& color) { return Rgb{color[0], color[1], color[2]}; };
    switch (input) {
    case BlendColor::Pixel:
        return toRgb(pixel);
    case BlendColor::Memory:
        return toRgb(inputs.memory);
    case BlendColor::Blend:
        return toRgb(inputs.blend);
    case BlendColor::Fog:
        return toRgb(inputs.fog);
    }
    return {};
}

u32 pickFirstWeight(const BlendAlpha input, const Color& pixel, const BlenderInputs& inputs) {
    switch (input) {
    case BlendAlpha::PixelAlpha:
        return pixel[3];
    case BlendAlpha::FogAlpha:
        return inputs.fog[3];
    case BlendAlpha::ShadeAlpha:
        return inputs.shadeAlpha;
    case BlendAlpha::Zero:
        return 0;
    }
    return 0;
}

u32 pickSecondWeight(const BlendWeight input, const u32 firstWeight, const BlenderInputs& inputs) {
    switch (input) {
    case BlendWeight::InverseAlpha:
        return ~firstWeight & 0xFF;
    case BlendWeight::MemoryAlpha:
        return inputs.memory[3];
    case BlendWeight::One:
        return 0xFF;
    case BlendWeight::Zero:
        return 0;
    }
    return 0;
}

Rgb blendCycle(const BlenderCycle& cycle, const Color& pixel, const BlenderInputs& inputs,
               const bool finalCycle, const bool forceBlend) {
    const Rgb first = pickColor(cycle.first, pixel, inputs);
    const bool opaque = cycle.firstWeight == BlendAlpha::PixelAlpha &&
                        cycle.secondWeight == BlendWeight::InverseAlpha && pixel[3] == 0xFF;
    if (finalCycle && (!forceBlend || opaque)) {
        return first;
    }
    const Rgb second = pickColor(cycle.second, pixel, inputs);
    const u32 rawFirstWeight = pickFirstWeight(cycle.firstWeight, pixel, inputs);
    u32 firstWeight = rawFirstWeight >> 3;
    u32 secondWeight = pickSecondWeight(cycle.secondWeight, rawFirstWeight, inputs) >> 3;
    if (cycle.secondWeight == BlendWeight::MemoryAlpha) {
        firstWeight &= 0x3C;
        secondWeight |= 0x03;
    }
    Rgb result{};
    for (usize channel = 0; channel < result.size(); ++channel) {
        const u32 blended = (first[channel] * firstWeight) + (second[channel] * (secondWeight + 1));
        result[channel] = static_cast<u8>(blended >> 5);
    }
    return result;
}

} // namespace

std::array<u8, 3> evaluateBlender(const OtherMode& otherMode, const BlenderInputs& inputs) {
    const bool forceBlend = otherMode.isForceBlend();
    Color pixel = inputs.pixel;
    if (otherMode.getCycleType() == CycleType::Two) {
        const Rgb first =
            blendCycle(otherMode.getBlenderCycle(0), pixel, inputs, false, forceBlend);
        pixel = {first[0], first[1], first[2], pixel[3]};
        return blendCycle(otherMode.getBlenderCycle(1), pixel, inputs, true, forceBlend);
    }
    return blendCycle(otherMode.getBlenderCycle(0), pixel, inputs, true, forceBlend);
}

} // namespace n64
