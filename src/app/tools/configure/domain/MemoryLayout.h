#pragma once

#include "app/tools/configure/Types.h"
#include "app/tools/configure/domain/ConfigSettings.h"

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::configure {

inline constexpr u32 RamStart = 0x80000000;
inline constexpr u32 RamEnd = 0x80800000;
/// Where RDRAM ends without an Expansion Pak.
inline constexpr u32 BaseRamEnd = 0x80400000;

/// What a region is, which is what it is coloured by.
enum class RegionKind : u8 {
    Code,
    Fixed,
    Gfx,
    Audio,
    Heap,
    Play,
    Object,
    GetItem,
    Framebuffer,
    Unused,
};

/// How a region shows on the strip of the whole RAM: across its height,
/// over a region drawn before it, inset along the bottom of one, or not at
/// all, as the Gfx buffers are, which the pools stand for.
enum class StripPlacement : u8 {
    Full,
    Over,
    Inset,
    None,
};

/// One region of RDRAM, or the boundary `SYS_CFB_END` is: what it is, the
/// setting that sizes it when one does, why it is where it is, and its
/// addresses. A region of `depth` lies inside the nearest region before it of
/// one less. `size` is what it costs, which for a Gfx buffer is both pools.
struct MemoryRegion {
    std::string label;
    /// The setting that sizes it, or empty when its size follows from others.
    std::string setting;
    /// Where its size comes from: a symbol, a sum, the build's map.
    std::string source;
    std::string reason;
    u32 start = 0;
    u32 end = 0;
    s64 size = 0;
    /// The vanilla size, from the setting's own comment, for a setting that
    /// names it.
    std::optional<s64> vanillaSize;
    usize depth = 0;
    RegionKind kind = RegionKind::Unused;
    StripPlacement placement = StripPlacement::None;
    /// Left over once the others are placed, as the heap Play does not take.
    bool isRemainder = false;
    bool isBoundary = false;
    bool isProblem = false;
};

/// RDRAM as the build lays it out under the configuration, in address order.
struct MemoryLayout {
    std::vector<MemoryRegion> regions;
    s64 systemHeapSize = 0;
    s64 playSize = 0;
    s64 actorSize = 0;
};

/// The symbols of the build's map the layout is placed by.
[[nodiscard]] std::span<const std::string_view> getMemoryMapSymbols();

/// The vanilla value a setting's comment gives, as `config_memory.h` writes
/// "Vanilla value is `0x17E0`" or "Vanilla is about `0xFA000`".
[[nodiscard]] std::optional<u32> findVanillaValue(std::string_view comment);

/// The layout of RDRAM the settings of `config_memory.h` give, each by what
/// the build ends with: `values` where one is edited, unless the safeguards
/// override it. What the configuration does not size, the code, the Z-buffer
/// and the RSP task buffers, is placed by `symbols`, read from the build's map.
[[nodiscard]] std::expected<MemoryLayout, std::string>
makeMemoryLayout(std::span<const ConfigSetting> settings,
                 const std::map<std::string, std::string, std::less<>>& values,
                 const std::map<std::string, u32, std::less<>>& symbols);

/// The region `index` lies inside, or none for one at the top.
[[nodiscard]] std::optional<usize> findParentRegion(std::span<const MemoryRegion> regions,
                                                    usize index);

} // namespace toki::configure
