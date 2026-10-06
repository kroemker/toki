#pragma once

#include "app/object/Types.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"

#include <array>
#include <vector>

namespace toki::object {

/// The `child` or `sibling` of a limb that has none: `LIMB_DONE`.
inline constexpr u8 NoLimb = 0xFF;

/// A `StandardLimb`, or the near display list of a `LodLimb`: where the limb's
/// joint is relative to its parent's, which limbs follow it, and what it draws.
struct Limb {
    std::array<s16, 3> jointPosition{};
    u8 child = NoLimb;
    u8 sibling = NoLimb;
    /// Zero for a limb that draws nothing.
    u32 displayList = 0;
};

/// A skeleton read from an object's bytes: a `SkeletonHeader`, or a
/// `FlexSkeletonHeader`, whose limbs' display lists may point at one
/// another's matrices through segment 13.
struct Skeleton {
    std::vector<Limb> limbs;
    bool isFlexible = false;
    usize displayListCount = 0;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the skeleton header at `address` and the limbs its limb table points
/// at. A `SkinLimb`, which the game draws another way, is read as drawing
/// nothing, and said so.
[[nodiscard]] Skeleton readSkeleton(const z64::MemoryImage& image, u32 address);

} // namespace toki::object
