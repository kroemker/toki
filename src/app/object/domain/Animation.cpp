#include "Animation.h"

#include "app/object/Types.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <vector>

namespace toki::object {

namespace {

constexpr usize HeaderSize = 0x10;
constexpr usize JointIndexSize = 6;

[[nodiscard]] std::optional<std::span<const u8>> findArray(const z64::MemoryImage& image,
                                                           const u32 address) {
    const std::optional<z64::ImageSymbol> symbol = image.findSymbolAt(address);
    if (!symbol) {
        return std::nullopt;
    }
    return image.findBytes(address, symbol->size);
}

} // namespace

std::expected<Animation, base::Diagnostic> readAnimation(const z64::MemoryImage& image,
                                                         const u32 address) {
    const std::optional<std::span<const u8>> header = image.findBytes(address, HeaderSize);
    if (!header) {
        return std::unexpected(base::makeError(
            {}, std::format("the animation at 0x{:08X} is outside the image", address)));
    }
    const u32 frameDataAddress = n64::readU32(*header, 4);
    const u32 jointIndicesAddress = n64::readU32(*header, 8);
    const std::optional<std::span<const u8>> frames = findArray(image, frameDataAddress);
    const std::optional<std::span<const u8>> joints = findArray(image, jointIndicesAddress);
    if (!frames || !joints) {
        return std::unexpected(base::makeError(
            {}, std::format("the frame data or joint indices of the animation at 0x{:08X} are not "
                            "arrays of this image",
                            address)));
    }

    Animation animation{.frameCount =
                            static_cast<usize>(std::max<s16>(n64::readS16(*header, 0), 1)),
                        .staticIndexMax = n64::readU16(*header, 12)};
    for (usize i = 0; i + 1 < frames->size(); i += 2) {
        animation.frameData.push_back(n64::readS16(*frames, i));
    }
    for (usize i = 0; i + JointIndexSize <= joints->size(); i += JointIndexSize) {
        animation.jointIndices.push_back(
            {n64::readU16(*joints, i), n64::readU16(*joints, i + 2), n64::readU16(*joints, i + 4)});
    }
    return animation;
}

JointTable makeJointTable(const Animation& animation, const usize frame) {
    const auto findValue = [&](const u16 index) -> s16 {
        const usize at = index >= animation.staticIndexMax ? usize{index} + frame : usize{index};
        return at < animation.frameData.size() ? animation.frameData[at] : s16{0};
    };
    JointTable table;
    table.reserve(animation.jointIndices.size());
    for (const std::array<u16, 3>& joint : animation.jointIndices) {
        table.push_back({findValue(joint[0]), findValue(joint[1]), findValue(joint[2])});
    }
    return table;
}

} // namespace toki::object
