#pragma once

#include "base/Diagnostic.h"
#include "n64/Memory.h"
#include "n64/Segment.h"
#include "z64/DataEncoder.h"
#include "z64/ImageTexture.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <array>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

/// One encoded object waiting for its place in a segment.
struct ImageObject {
    u8 segment = 0;
    std::string name;
    /// The type the object was declared with, as the C spells it before any
    /// array bound: `Gfx` for `Gfx sDL[]`, `FlexSkeletonHeader`, `u64`.
    std::string type;
    bool isArray = false;
    usize alignment = 1;

    /// The object is the first of its file, which starts on the boundary the
    /// linker gives every file.
    bool startsFile = false;
    /// The texture an embedded image or palette made of the object.
    std::optional<ImageTexture> texture;
    EncodedData data;
    base::SourceLocation location;
};

/// Where a laid-out symbol is: a segment and an offset into it, which is the
/// segmented address the game reads it through.
struct ImageSymbol {
    u8 segment = 0;
    usize offset = 0;
    usize size = 0;
    /// As `ImageObject::type` and `ImageObject::isArray` declared it.
    std::string type;
    bool isArray = false;
    std::optional<ImageTexture> texture;
};

/// One segment's bytes with every address patched in, and the relocations
/// that were patched, so the bytes before patching can be recovered.
struct ImageSegment {
    u8 number = 0;
    std::vector<u8> bytes;
    std::vector<Relocation> relocations;
};

/// Assets as the game sees them in memory: per-segment buffers, the way
/// `gSPSegment` maps a scene to segment 2 and a room to segment 3, and the
/// symbols laid out in them.
struct MemoryImage {
    std::vector<ImageSegment> segments;
    std::map<std::string, ImageSymbol, std::less<>> symbols;
    std::vector<base::Diagnostic> diagnostics;

    [[nodiscard]] const ImageSegment* findSegment(u8 number) const;

    [[nodiscard]] std::optional<ImageSymbol> findSymbol(std::string_view name) const;

    /// The segmented address of a symbol.
    [[nodiscard]] std::optional<u32> findAddress(std::string_view name) const;

    /// The symbol that starts at a segmented address, if one does.
    [[nodiscard]] std::optional<ImageSymbol> findSymbolAt(u32 address) const;

    /// The `size` bytes at a segmented address, if one segment holds all of
    /// them.
    [[nodiscard]] std::optional<std::span<const u8>> findBytes(u32 address, usize size) const;
};

struct LayoutOptions {
    /// What every file starts and ends on within its segment, as the
    /// assembler pads each object file's data and the linker aligns it.
    usize fileAlignment = 0x10;

    /// Addresses for symbols no object defines, such as data in the game's
    /// code. A symbol neither defines is a warning naming it, since an
    /// object may point into one that is not in the image, and its words keep
    /// only what was added to the address.
    std::function<std::optional<u32>(std::string_view)> findExternal;
};

/// Lays objects out in the order given, each segment from offset zero, every
/// object at its alignment, and patches each relocation with the segmented
/// address of its symbol.
[[nodiscard]] MemoryImage layOutImage(std::vector<ImageObject> objects,
                                      const LayoutOptions& options);

/// Where `mapImageAt` put an image: the physical address of each segment it
/// has, zero for those it lacks, and the first free address after it.
struct MappedImage {
    std::array<u32, n64::SegmentCount> segments{};
    u32 end = 0;
};

/// Maps every segment of `image` into `memory`, one after another from
/// `physicalAddress`, each on a 64 KiB boundary, except segment 0: the game
/// code's data, which is mapped at physical address 0, so that an address in it
/// is physical, as the game's own code addresses are, and segment 0 points at 0
/// as on the N64. The image must outlive the memory. Several images map into
/// one memory when each starts where the previous one ended.
[[nodiscard]] std::expected<MappedImage, base::Diagnostic>
mapImageAt(const MemoryImage& image, n64::Memory& memory, u32 physicalAddress);

/// Maps every segment of `image` into `memory` at its own physical address,
/// and gives the segment table that points each segment number at it. The
/// image must outlive the memory.
[[nodiscard]] std::expected<std::array<u32, n64::SegmentCount>, base::Diagnostic>
mapImage(const MemoryImage& image, n64::Memory& memory);

} // namespace z64
