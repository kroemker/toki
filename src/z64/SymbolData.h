#pragma once

#include "z64/EmbeddedSource.h"
#include "z64/ImageTexture.h"
#include "z64/MemoryImage.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

/// One symbol of a memory image as the C that declared it said it: its type,
/// its bytes as they were before addresses were patched in, and its
/// relocations, each at an offset from the symbol's start, naming the symbol
/// its word points at, with the constant added to the address in the word.
/// That is all a declaration says that the game sees, so the declaration can
/// be written again from it.
struct SymbolData {
    std::string name;
    u8 segment = 0;
    /// Where the image laid the symbol out, which orders a segment's symbols.
    usize offset = 0;
    std::string type;
    bool isArray = false;
    std::vector<u8> bytes;
    std::vector<Relocation> relocations;
    std::optional<ImageTexture> texture;
    std::optional<EmbeddedSource> embedded;

    [[nodiscard]] bool operator==(const SymbolData&) const = default;
};

/// Every symbol `image` laid out in `segment`, in the order it laid them out.
[[nodiscard]] std::vector<SymbolData> listSymbolData(const MemoryImage& image, u8 segment);

/// The symbol `name` of `image`, if it has one.
[[nodiscard]] std::optional<SymbolData> findSymbolData(const MemoryImage& image,
                                                       std::string_view name);

/// The object that lays `symbol` out again, at `alignment` and starting a
/// file when `startsFile`.
[[nodiscard]] ImageObject makeImageObject(const SymbolData& symbol, usize alignment,
                                          bool startsFile);

} // namespace z64
