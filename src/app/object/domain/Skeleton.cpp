#include "Skeleton.h"

#include "app/object/Types.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"

#include <format>
#include <optional>
#include <span>

namespace toki::object {

namespace {

constexpr usize HeaderSize = 8;
constexpr usize FlexibleHeaderSize = 12;
constexpr usize LimbSize = 12;
constexpr usize PointerSize = 4;

} // namespace

Skeleton readSkeleton(const z64::MemoryImage& image, const u32 address) {
    Skeleton skeleton;
    const std::optional<z64::ImageSymbol> symbol = image.findSymbolAt(address);
    skeleton.isFlexible = symbol && symbol->type == "FlexSkeletonHeader";
    const std::optional<std::span<const u8>> header =
        image.findBytes(address, skeleton.isFlexible ? FlexibleHeaderSize : HeaderSize);
    if (!header) {
        skeleton.diagnostics.push_back(base::makeError(
            {}, std::format("the skeleton at 0x{:08X} is outside the image", address)));
        return skeleton;
    }
    const u32 limbTable = n64::readU32(*header, 0);
    const usize limbCount = (*header)[4];
    if (skeleton.isFlexible) {
        skeleton.displayListCount = (*header)[8];
    }

    const std::optional<std::span<const u8>> table =
        image.findBytes(limbTable, limbCount * PointerSize);
    if (!table) {
        skeleton.diagnostics.push_back(base::makeError(
            {}, std::format("the limb table at 0x{:08X} of the skeleton at 0x{:08X} is outside "
                            "the image",
                            limbTable, address)));
        return skeleton;
    }
    for (usize i = 0; i < limbCount; ++i) {
        const u32 limbAddress = n64::readU32(*table, i * PointerSize);
        const std::optional<std::span<const u8>> bytes = image.findBytes(limbAddress, LimbSize);
        if (!bytes) {
            skeleton.diagnostics.push_back(base::makeError(
                {}, std::format("limb {} at 0x{:08X} is outside the image", i, limbAddress)));
            skeleton.limbs.emplace_back();
            continue;
        }
        Limb limb{.jointPosition = {n64::readS16(*bytes, 0), n64::readS16(*bytes, 2),
                                    n64::readS16(*bytes, 4)},
                  .child = (*bytes)[6],
                  .sibling = (*bytes)[7],
                  .displayList = n64::readU32(*bytes, 8)};
        const std::optional<z64::ImageSymbol> limbSymbol = image.findSymbolAt(limbAddress);
        if (limbSymbol && limbSymbol->type == "SkinLimb") {
            limb.displayList = 0;
            skeleton.diagnostics.push_back(base::makeWarning(
                {}, std::format("limb {} is a SkinLimb, which is not drawn yet", i)));
        }
        skeleton.limbs.push_back(limb);
    }
    return skeleton;
}

} // namespace toki::object
