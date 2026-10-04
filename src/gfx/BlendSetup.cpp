#include "BlendSetup.h"

#include "n64/OtherMode.h"

#include <expected>
#include <string>

namespace gfx {

namespace {

using n64::BlendColor;
using n64::BlendWeight;

std::expected<BlendFactor, std::string> findDestinationFactor(const BlendWeight weight) {
    switch (weight) {
    case BlendWeight::InverseAlpha:
        return BlendFactor::OneMinusSourceAlpha;
    case BlendWeight::MemoryAlpha:
        return BlendFactor::DestinationAlpha;
    case BlendWeight::One:
        return BlendFactor::One;
    case BlendWeight::Zero:
        return BlendFactor::Zero;
    }
    return std::unexpected("the blender's second weight is not one the RDP knows");
}

} // namespace

std::expected<BlendSetup, std::string> makeBlendSetup(const n64::OtherMode& otherMode) {
    const n64::CycleType cycleType = otherMode.getCycleType();
    if (cycleType == n64::CycleType::Copy || cycleType == n64::CycleType::Fill) {
        return BlendSetup{};
    }
    const bool twoCycle = cycleType == n64::CycleType::Two;
    if (twoCycle) {
        const n64::BlenderCycle first = otherMode.getBlenderCycle(0);
        if (first.first == BlendColor::Memory || first.second == BlendColor::Memory ||
            first.secondWeight == BlendWeight::MemoryAlpha) {
            return std::unexpected("the first blender cycle reads the framebuffer");
        }
    }

    const n64::BlenderCycle cycle = otherMode.getBlenderCycle(twoCycle ? 1 : 0);
    const bool firstIsMemory = cycle.first == BlendColor::Memory;
    const bool secondIsMemory = cycle.second == BlendColor::Memory;
    if (!otherMode.isForceBlend()) {
        if (firstIsMemory) {
            return BlendSetup{.path = BlendPath::KeepMemory,
                              .enabled = true,
                              .source = BlendFactor::Zero,
                              .destination = BlendFactor::One};
        }
        return BlendSetup{};
    }
    if (!firstIsMemory && !secondIsMemory) {
        if (cycle.secondWeight == BlendWeight::MemoryAlpha) {
            return std::unexpected("the blender weighs by memory alpha without blending memory");
        }
        return BlendSetup{};
    }
    if (firstIsMemory && secondIsMemory) {
        return std::unexpected("the blender weighs the framebuffer against itself");
    }
    if (secondIsMemory) {
        const std::expected<BlendFactor, std::string> destination =
            findDestinationFactor(cycle.secondWeight);
        if (!destination) {
            return std::unexpected(destination.error());
        }
        return BlendSetup{.path = BlendPath::OverMemory,
                          .enabled = true,
                          .source = BlendFactor::SourceAlpha,
                          .destination = *destination};
    }
    if (cycle.secondWeight != BlendWeight::InverseAlpha) {
        return std::unexpected(
            "the blender weighs the framebuffer first, against something other than 1 - A");
    }
    return BlendSetup{.path = BlendPath::MemoryUnder,
                      .enabled = true,
                      .source = BlendFactor::SourceAlpha,
                      .destination = BlendFactor::OneMinusSourceAlpha};
}

} // namespace gfx
