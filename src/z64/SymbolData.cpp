#include "SymbolData.h"

#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace z64 {

namespace {

constexpr usize WordSize = 4;

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

SymbolData makeSymbolData(const ImageSegment& segment, const std::string_view name,
                          const ImageSymbol& symbol) {
    SymbolData data{
        .name = std::string(name),
        .segment = symbol.segment,
        .offset = symbol.offset,
        .type = symbol.type,
        .isArray = symbol.isArray,
        .pointerDepth = symbol.pointerDepth,
        .innerLengths = symbol.innerLengths,
        .alignment = symbol.alignment,
        .startsFile = symbol.startsFile,
        .texture = symbol.texture,
        .embedded = symbol.embedded,
    };
    const usize end = std::min(symbol.offset + symbol.size, segment.bytes.size());
    if (symbol.offset < end) {
        data.bytes.assign(segment.bytes.begin() + static_cast<std::ptrdiff_t>(symbol.offset),
                          segment.bytes.begin() + static_cast<std::ptrdiff_t>(end));
    }
    const auto first =
        std::ranges::lower_bound(segment.relocations, symbol.offset, {}, &Relocation::offset);
    for (auto relocation = first;
         relocation != segment.relocations.end() && relocation->offset < end; ++relocation) {
        const usize at = relocation->offset - symbol.offset;
        if (at + WordSize > data.bytes.size()) {
            continue;
        }
        writeWord(data.bytes, at, readWord(data.bytes, at) - relocation->address);
        data.relocations.push_back(Relocation{.offset = at, .symbol = relocation->symbol});
    }
    return data;
}

bool isSorted(const ImageSegment& segment) {
    return std::ranges::is_sorted(segment.relocations, {}, &Relocation::offset);
}

} // namespace

std::vector<SymbolData> listSymbolData(const MemoryImage& image, const u8 segment) {
    const ImageSegment* found = image.findSegment(segment);
    if (found == nullptr) {
        return {};
    }
    ImageSegment sorted;
    const ImageSegment* bytes = found;
    if (!isSorted(*found)) {
        sorted = *found;
        std::ranges::stable_sort(sorted.relocations, {}, &Relocation::offset);
        bytes = &sorted;
    }
    std::vector<SymbolData> symbols;
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.segment == segment) {
            symbols.push_back(makeSymbolData(*bytes, name, symbol));
        }
    }
    std::ranges::stable_sort(symbols, {}, &SymbolData::offset);
    return symbols;
}

std::optional<SymbolData> findSymbolData(const MemoryImage& image, const std::string_view name) {
    const auto symbol = image.symbols.find(name);
    if (symbol == image.symbols.end()) {
        return std::nullopt;
    }
    const ImageSegment* segment = image.findSegment(symbol->second.segment);
    if (segment == nullptr) {
        return std::nullopt;
    }
    if (!isSorted(*segment)) {
        ImageSegment sorted = *segment;
        std::ranges::stable_sort(sorted.relocations, {}, &Relocation::offset);
        return makeSymbolData(sorted, name, symbol->second);
    }
    return makeSymbolData(*segment, name, symbol->second);
}

ImageObject makeImageObject(const SymbolData& symbol) {
    return ImageObject{
        .segment = symbol.segment,
        .name = symbol.name,
        .type = symbol.type,
        .isArray = symbol.isArray,
        .pointerDepth = symbol.pointerDepth,
        .innerLengths = symbol.innerLengths,
        .alignment = symbol.alignment,
        .startsFile = symbol.startsFile,
        .texture = symbol.texture,
        .embedded = symbol.embedded,
        .data = EncodedData{.bytes = symbol.bytes, .relocations = symbol.relocations},
    };
}

} // namespace z64
