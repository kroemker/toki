#include "TextureMemory.h"

#include "base/Diagnostic.h"
#include "n64/Memory.h"
#include "n64/Opcode.h"
#include "n64/RspResult.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <format>
#include <optional>
#include <span>
#include <string_view>

namespace n64 {

namespace {

constexpr u32 FullMask = 0x0FFF;
constexpr u32 HalfMask = 0x07FF;
constexpr u32 UpperHalf = 0x0800;
constexpr u32 OddRowSwap = 0x4;
constexpr u32 DxtOne = 0x0800;

bool isRgba32(const TileDescriptor& tile) {
    return tile.format == TextureFormat::Rgba && tile.size == TexelSize::Bits32;
}

u32 getSizeShift(const TexelSize size) {
    return static_cast<u32>(size);
}

u32 getBytesPerRow(const TextureImage& image) {
    return (image.width << getSizeShift(image.size)) >> 1;
}

TileAxis makeAxis(const TileAxis& previous, const u32 bits) {
    return TileAxis{
        .clamp = (bits & 0x200) != 0,
        .mirror = (bits & 0x100) != 0,
        .mask = static_cast<u8>((bits >> 4) & 0x0F),
        .shift = static_cast<u8>(bits & 0x0F),
        .low = previous.low,
        .high = previous.high,
    };
}

void setTileSize(TileDescriptor& tile, const u32 word0, const u32 word1) {
    tile.axes[0].low = static_cast<u16>((word0 >> 12) & 0x0FFF);
    tile.axes[1].low = static_cast<u16>(word0 & 0x0FFF);
    tile.axes[0].high = static_cast<u16>((word1 >> 12) & 0x0FFF);
    tile.axes[1].high = static_cast<u16>(word1 & 0x0FFF);
}

} // namespace

const TileDescriptor& TextureMemory::getTile(const usize index) const {
    return tiles[index % TileCount];
}

std::optional<base::Diagnostic> TextureMemory::apply(const RdpCommand& command,
                                                     const Memory& memory) {
    const u32 word0 = command.word0;
    const u32 word1 = command.word1;
    bool complete = true;
    std::string_view name;
    switch (static_cast<Opcode>(word0 >> 24)) {
    case Opcode::SetTextureImage:
        textureImage = TextureImage{
            .format = static_cast<TextureFormat>((word0 >> 21) & 0x07),
            .size = static_cast<TexelSize>((word0 >> 19) & 0x03),
            .width = (word0 & 0x0FFF) + 1,
            .address = word1 & 0x00FF'FFFF,
        };
        break;
    case Opcode::SetTile:
        setTile(word0, word1);
        break;
    case Opcode::SetTileSize:
        setTileSize(tiles[(word1 >> 24) & 0x07], word0, word1);
        break;
    case Opcode::LoadTile:
        name = "G_LOADTILE";
        complete = loadTile(word0, word1, memory);
        break;
    case Opcode::LoadBlock:
        name = "G_LOADBLOCK";
        complete = loadBlock(word0, word1, memory);
        break;
    case Opcode::LoadTlut:
        name = "G_LOADTLUT";
        complete = loadTlut(word0, word1, memory);
        break;
    default:
        break;
    }
    if (complete) {
        return std::nullopt;
    }
    return base::makeWarning(
        {}, std::format("RDP command at 0x{:06X}: {} reads outside mapped memory near 0x{:06X}",
                        command.address, name, textureImage.address));
}

void TextureMemory::setTile(const u32 word0, const u32 word1) {
    TileDescriptor& tile = tiles[(word1 >> 24) & 0x07];
    tile.format = static_cast<TextureFormat>((word0 >> 21) & 0x07);
    tile.size = static_cast<TexelSize>((word0 >> 19) & 0x03);
    tile.line = static_cast<u16>((word0 >> 9) & 0x01FF);
    tile.address = static_cast<u16>(word0 & 0x01FF);
    tile.palette = static_cast<u8>((word1 >> 20) & 0x0F);
    tile.axes[0] = makeAxis(tile.axes[0], word1 & 0x03FF);
    tile.axes[1] = makeAxis(tile.axes[1], (word1 >> 10) & 0x03FF);
}

bool TextureMemory::loadTile(const u32 word0, const u32 word1, const Memory& memory) {
    TileDescriptor& tile = tiles[(word1 >> 24) & 0x07];
    setTileSize(tile, word0, word1);
    const TileAxis& s = tile.axes[0];
    const TileAxis& t = tile.axes[1];
    if (s.low > s.high || t.low > t.high) {
        return true;
    }
    const u32 bytesPerRow = getBytesPerRow(textureImage);
    const u32 offset = ((u32{s.low} >> 2) << getSizeShift(textureImage.size)) >> 1;
    return loadWords(
        tile,
        LoadRegion{
            .source = textureImage.address + offset + (bytesPerRow * (t.low >> 2)),
            .sourceStride = bytesPerRow,
            .rows = 1 + (u32{t.high} >> 2) - (u32{t.low} >> 2),
            .wordsPerRow =
                (((u32{s.high} >> 2) - (u32{s.low} >> 2)) >> (4 - getSizeShift(tile.size))) + 1,
        },
        memory, false);
}

bool TextureMemory::loadBlock(const u32 word0, const u32 word1, const Memory& memory) {
    TileDescriptor& tile = tiles[(word1 >> 24) & 0x07];
    setTileSize(tile, word0, word1);
    const u32 low = tile.axes[0].low;
    const u32 high = tile.axes[0].high;
    if (low > high || high >= 0x0800) {
        return true;
    }
    const u32 bytesPerRow = getBytesPerRow(textureImage);
    const u32 offset = (low << getSizeShift(textureImage.size)) >> 1;
    return loadWords(tile,
                     LoadRegion{
                         .source = textureImage.address + offset + (bytesPerRow * tile.axes[1].low),
                         .wordsPerRow = ((high - low) >> (4 - getSizeShift(tile.size))) + 1,
                         .dxt = tile.axes[1].high,
                     },
                     memory, false);
}

bool TextureMemory::loadTlut(const u32 word0, const u32 word1, const Memory& memory) {
    TileDescriptor& tile = tiles[(word1 >> 24) & 0x07];
    setTileSize(tile, word0, word1);
    const TileAxis& s = tile.axes[0];
    const TileAxis& t = tile.axes[1];
    if (s.low > s.high || t.low > t.high) {
        return true;
    }
    const u32 bytesPerRow = getBytesPerRow(textureImage);
    const u32 offset = ((u32{s.low} >> 2) << getSizeShift(textureImage.size)) >> 1;
    return loadWords(tile,
                     LoadRegion{
                         .source = textureImage.address + offset + (bytesPerRow * (t.low >> 2)),
                         .sourceStride = bytesPerRow,
                         .sourceAdvance = 2,
                         .rows = 1 + (u32{t.high} >> 2) - (u32{t.low} >> 2),
                         .wordsPerRow = (u32{s.high} >> 2) - (u32{s.low} >> 2) + 1,
                     },
                     memory, true);
}

bool TextureMemory::loadWords(const TileDescriptor& tile, const LoadRegion& region,
                              const Memory& memory, const bool tlut) {
    const bool split = isRgba32(tile);
    const u32 mask = split ? HalfMask : FullMask;
    const u32 advance = split ? 4 : 8;
    const u32 stride = u32{tile.line} << (tlut ? 5 : 3);
    const usize readSize = tlut ? 2 : 8;
    bool complete = true;
    u32 swap = 0;
    u32 dxtCounter = 0;
    u32 rowSource = region.source;
    u32 rowTarget = (u32{tile.address} << 3) & mask;
    for (u32 row = 0; row < region.rows; ++row) {
        u32 source = rowSource;
        u32 target = rowTarget;
        for (u32 word = 0; word < region.wordsPerRow; ++word) {
            if (const auto read = memory.findBytes(source, readSize)) {
                storeWord(*read, target, swap, split);
            } else {
                complete = false;
            }
            dxtCounter += region.dxt;
            while (dxtCounter >= DxtOne) {
                target = (target + stride) & mask;
                dxtCounter -= DxtOne;
                swap ^= OddRowSwap;
            }
            source += region.sourceAdvance;
            target = (target + advance) & mask;
        }
        rowTarget = (rowTarget + stride) & mask;
        rowSource += region.sourceStride;
        swap ^= OddRowSwap;
    }
    return complete;
}

void TextureMemory::storeWord(const std::span<const u8> word, const u32 target, const u32 swap,
                              const bool split) {
    if (split) {
        for (u32 i = 0; i < 4; ++i) {
            const u32 pick = (i & 1) | ((i & 2) << 1);
            const u32 index = (target + i) ^ swap;
            bytes[index] = word[pick];
            bytes[index | UpperHalf] = word[pick | 2];
        }
        return;
    }
    for (u32 i = 0; i < 8; ++i) {
        bytes[((target + i) ^ swap) & FullMask] = word[i % word.size()];
    }
}

} // namespace n64
