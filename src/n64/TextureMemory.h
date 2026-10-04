#pragma once

#include "base/Diagnostic.h"
#include "n64/Memory.h"
#include "n64/RspResult.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <array>
#include <optional>
#include <span>

namespace n64 {

inline constexpr usize TextureMemorySize = 4096;
inline constexpr usize TileCount = 8;

/// The RDP's 4 KB of texture memory and the tile descriptors that address
/// it, changed by the commands that set and load them. Loads behave as the
/// hardware's do, byte for byte: `G_LOADBLOCK` swaps the 32-bit halves of the
/// words of every odd row its `dxt` counts, 32-bit RGBA is split between the
/// two halves of the memory, and `G_LOADTLUT` writes each palette entry four
/// times into a 64-bit word.
class TextureMemory {
public:
    /// Applies `command` if it is one of `G_SETTIMG`, `G_SETTILE`,
    /// `G_SETTILESIZE`, `G_LOADTILE`, `G_LOADBLOCK` or `G_LOADTLUT`, reading
    /// from `memory` for loads. Any other command is ignored. A load that
    /// reads outside mapped memory skips what it cannot read and is reported.
    [[nodiscard]] std::optional<base::Diagnostic> apply(const RdpCommand& command,
                                                        const Memory& memory);

    [[nodiscard]] const TileDescriptor& getTile(usize index) const;

    [[nodiscard]] const TextureImage& getTextureImage() const {
        return textureImage;
    }

    [[nodiscard]] std::span<const u8, TextureMemorySize> getBytes() const {
        return bytes;
    }

private:
    struct LoadRegion {
        u32 source = 0;
        u32 sourceStride = 0;
        u32 sourceAdvance = 8;
        u32 rows = 1;
        u32 wordsPerRow = 0;
        u32 dxt = 0;
    };

    void setTile(u32 word0, u32 word1);
    [[nodiscard]] bool loadTile(u32 word0, u32 word1, const Memory& memory);
    [[nodiscard]] bool loadBlock(u32 word0, u32 word1, const Memory& memory);
    [[nodiscard]] bool loadTlut(u32 word0, u32 word1, const Memory& memory);
    [[nodiscard]] bool loadWords(const TileDescriptor& tile, const LoadRegion& region,
                                 const Memory& memory, bool tlut);
    void storeWord(std::span<const u8> word, u32 target, u32 swap, bool split);

    std::array<u8, TextureMemorySize> bytes{};
    std::array<TileDescriptor, TileCount> tiles{};
    TextureImage textureImage;
};

} // namespace n64
