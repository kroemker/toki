#pragma once

#include "base/Diagnostic.h"
#include "n64/Types.h"

#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace n64 {

/// The physical address space the RSP and RDP read from: the stand-in for
/// RDRAM. Rather than one flat 8 MB buffer it is a set of regions, each a
/// borrowed byte span placed at a physical address, so a segment buffer can be
/// mapped where the segment table says it is without being copied. Bytes are
/// in N64 order, big-endian.
///
/// Physical addresses are 24 bits, as the RSP's DMA engine sees them.
class Memory {
public:
    /// Places `bytes` at `physicalAddress`. The bytes are borrowed and must
    /// outlive the memory. A region that overlaps another, or runs past the
    /// 24-bit address space, is refused.
    [[nodiscard]] std::expected<void, base::Diagnostic> map(u32 physicalAddress,
                                                            std::span<const u8> bytes);

    /// The `size` bytes at `physicalAddress`, if one region holds all of them.
    [[nodiscard]] std::optional<std::span<const u8>> findBytes(u32 physicalAddress,
                                                               usize size) const;

private:
    struct Region {
        u32 address = 0;
        std::span<const u8> bytes;
    };

    std::vector<Region> regions;
};

} // namespace n64
