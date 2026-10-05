#include "MemoryImage.h"

#include "base/Diagnostic.h"
#include "n64/Memory.h"
#include "n64/Segment.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr usize SegmentRegionAlignment = 0x10000;
constexpr u32 FirstPhysicalAddress = 0x100000;

usize alignUp(const usize value, const usize alignment) {
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

u32 readWord(const std::vector<u8>& bytes, const usize offset) {
    return (u32{bytes[offset]} << 24) | (u32{bytes[offset + 1]} << 16) |
           (u32{bytes[offset + 2]} << 8) | u32{bytes[offset + 3]};
}

void writeWord(std::vector<u8>& bytes, const usize offset, const u32 word) {
    bytes[offset] = static_cast<u8>(word >> 24);
    bytes[offset + 1] = static_cast<u8>(word >> 16);
    bytes[offset + 2] = static_cast<u8>(word >> 8);
    bytes[offset + 3] = static_cast<u8>(word);
}

ImageSegment& findOrAddSegment(std::vector<ImageSegment>& segments, const u8 number) {
    const auto found = std::ranges::find(segments, number, &ImageSegment::number);
    if (found != segments.end()) {
        return *found;
    }
    return segments.emplace_back(ImageSegment{.number = number});
}

} // namespace

const ImageSegment* MemoryImage::findSegment(const u8 number) const {
    const auto found = std::ranges::find(segments, number, &ImageSegment::number);
    return found == segments.end() ? nullptr : &*found;
}

std::optional<ImageSymbol> MemoryImage::findSymbol(const std::string_view name) const {
    const auto found = symbols.find(name);
    return found == symbols.end() ? std::nullopt : std::optional(found->second);
}

std::optional<u32> MemoryImage::findAddress(const std::string_view name) const {
    const std::optional<ImageSymbol> symbol = findSymbol(name);
    if (!symbol) {
        return std::nullopt;
    }
    return n64::pack(n64::SegmentAddress{.segment = symbol->segment,
                                         .offset = static_cast<u32>(symbol->offset)});
}

std::optional<ImageSymbol> MemoryImage::findSymbolAt(const u32 address) const {
    const n64::SegmentAddress segmented = n64::unpack(address);
    const auto found = std::ranges::find_if(symbols, [&segmented](const auto& entry) {
        return entry.second.segment == segmented.segment && entry.second.offset == segmented.offset;
    });
    return found == symbols.end() ? std::nullopt : std::optional(found->second);
}

std::optional<std::span<const u8>> MemoryImage::findBytes(const u32 address,
                                                          const usize size) const {
    const n64::SegmentAddress segmented = n64::unpack(address);
    const ImageSegment* segment = findSegment(segmented.segment);
    if (segment == nullptr || segmented.offset > segment->bytes.size() ||
        size > segment->bytes.size() - segmented.offset) {
        return std::nullopt;
    }
    return std::span<const u8>(segment->bytes).subspan(segmented.offset, size);
}

MemoryImage layOutImage(std::vector<ImageObject> objects, const LayoutOptions& options) {
    MemoryImage image;
    std::map<u8, std::vector<const ImageObject*>> owners;
    for (ImageObject& object : objects) {
        ImageSegment& segment = findOrAddSegment(image.segments, object.segment);
        usize offset = alignUp(segment.bytes.size(), object.alignment);
        if (object.startsFile) {
            offset = alignUp(offset, options.fileAlignment);
        }
        segment.bytes.resize(offset, 0);
        segment.bytes.insert(segment.bytes.end(), object.data.bytes.begin(),
                             object.data.bytes.end());
        for (Relocation& relocation : object.data.relocations) {
            relocation.offset += offset;
            segment.relocations.push_back(std::move(relocation));
            owners[object.segment].push_back(&object);
        }
        const auto [found, isNew] =
            image.symbols.try_emplace(object.name, ImageSymbol{.segment = object.segment,
                                                               .offset = offset,
                                                               .size = object.data.bytes.size()});
        if (!isNew) {
            image.diagnostics.push_back(base::makeError(
                object.location, std::format("'{}' is defined twice", object.name)));
        }
        for (base::Diagnostic& diagnostic : object.data.diagnostics) {
            image.diagnostics.push_back(std::move(diagnostic));
        }
    }

    for (ImageSegment& segment : image.segments) {
        segment.bytes.resize(alignUp(segment.bytes.size(), options.fileAlignment), 0);
        for (usize i = 0; i < segment.relocations.size(); ++i) {
            const Relocation& relocation = segment.relocations[i];
            std::optional<u32> address = image.findAddress(relocation.symbol);
            if (!address && options.findExternal) {
                address = options.findExternal(relocation.symbol);
            }
            if (!address) {
                const ImageObject& owner = *owners[segment.number][i];
                image.diagnostics.push_back(base::makeError(
                    owner.location, std::format("'{}' points at '{}', which is defined nowhere",
                                                owner.name, relocation.symbol)));
                continue;
            }
            writeWord(segment.bytes, relocation.offset,
                      readWord(segment.bytes, relocation.offset) + *address);
        }
    }
    return image;
}

std::expected<MappedImage, base::Diagnostic>
mapImageAt(const MemoryImage& image, n64::Memory& memory, const u32 physicalAddress) {
    MappedImage mapped;
    u32 physical = static_cast<u32>(alignUp(physicalAddress, SegmentRegionAlignment));
    for (const ImageSegment& segment : image.segments) {
        if (segment.number >= n64::SegmentCount) {
            return std::unexpected(base::makeError(
                {}, std::format("segment {} is not one the RSP has", segment.number)));
        }
        if (segment.number == 0) {
            if (const std::expected<void, base::Diagnostic> placed = memory.map(0, segment.bytes);
                !placed) {
                return std::unexpected(placed.error());
            }
            continue;
        }
        if (const std::expected<void, base::Diagnostic> placed =
                memory.map(physical, segment.bytes);
            !placed) {
            return std::unexpected(placed.error());
        }
        mapped.segments[segment.number] = physical;
        physical = static_cast<u32>(
            alignUp(physical + std::max<usize>(segment.bytes.size(), 1), SegmentRegionAlignment));
    }
    mapped.end = physical;
    return mapped;
}

std::expected<std::array<u32, n64::SegmentCount>, base::Diagnostic>
mapImage(const MemoryImage& image, n64::Memory& memory) {
    std::expected<MappedImage, base::Diagnostic> mapped =
        mapImageAt(image, memory, FirstPhysicalAddress);
    if (!mapped) {
        return std::unexpected(std::move(mapped.error()));
    }
    return mapped->segments;
}

} // namespace z64
