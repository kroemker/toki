#pragma once

#include "app/object/Types.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"

#include <array>
#include <expected>
#include <vector>

namespace toki::object {

/// An `AnimationHeader` with the data it points at: for every joint, where to
/// find its three values in `frameData`, either once for the whole
/// animation or once per frame.
struct Animation {
    usize frameCount = 0;
    std::vector<s16> frameData;
    std::vector<std::array<u16, 3>> jointIndices;
    u16 staticIndexMax = 0;
};

/// A pose as the game's `jointTable` holds one: the root's translation, then
/// every limb's rotation in binary angles.
using JointTable = std::vector<std::array<s16, 3>>;

/// Reads the animation at `address`. Its arrays carry no length of their own,
/// so they are as long as the arrays the image laid out at their addresses.
[[nodiscard]] std::expected<Animation, base::Diagnostic>
readAnimation(const z64::MemoryImage& image, u32 address);

/// Frame `frame` of `animation`, as `SkelAnime_GetFrameData` reads it: an
/// index at or past `staticIndexMax` takes the value for this frame, any other
/// the value that holds throughout. A value past the end of the data reads as
/// zero.
[[nodiscard]] JointTable makeJointTable(const Animation& animation, usize frame);

} // namespace toki::object
