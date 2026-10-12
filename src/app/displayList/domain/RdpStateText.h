#pragma once

#include "app/displayList/Types.h"
#include "n64/Rdp.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::displayList {

/// One setting of the RDP's state, as a person reads it: what it is, its value
/// in words, and what it means.
struct StateLine {
    std::string label;
    std::string value;
    std::string_view description;
};

/// A part of the RDP's state, under a title and the glossary term it is about.
struct StateSection {
    std::string title;
    std::string_view term;
    std::vector<StateLine> lines;
};

/// The state a primitive was drawn with, part by part: the colour combiner of
/// each cycle the cycle type uses, as `(a − b) × c + d` and as it reduces, the
/// blender, the other mode's settings, the colours, and every tile descriptor
/// that has been set up.
[[nodiscard]] std::vector<StateSection> describeRdpState(const n64::RdpState& state);

/// A tile's texels as `fetchTexel` reads them out of texture memory, row by row
/// from the top, four bytes a texel; its extent is the tile's, clamped to 256.
struct TilePreview {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels;
};

/// What tile `tile` of `state` shows out of `memory`, or nothing for a tile no
/// load has set up.
[[nodiscard]] std::optional<TilePreview>
makeTilePreview(const n64::RdpState& state, const n64::TextureMemoryImage& memory, usize tile);

/// What one of the terms a display list is read with means: `texture memory`,
/// `tile descriptor`, `cycle`, `combiner`, `blender`, `other mode`,
/// `geometry mode`, `segment`, `vertex buffer`.
[[nodiscard]] std::string_view findGlossaryEntry(std::string_view term);

} // namespace toki::displayList
