#include "RdpStateText.h"

#include "app/displayList/Types.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/CommandDecoder.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/TexelFetch.h"
#include "n64/TileDescriptor.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::displayList {

namespace {

constexpr u32 PreviewLimit = 256;
constexpr std::array<std::string_view, 5> FormatNames{"RGBA", "YUV", "CI", "IA", "I"};
constexpr std::array<std::string_view, 4> SizeNames{"4", "8", "16", "32"};

struct Term {
    std::string_view name;
    std::string_view meaning;
};

constexpr std::array<Term, 9> Glossary{{
    {"texture memory", "TMEM: the RDP's own 4 KB of memory that textures are drawn from. A "
                       "texture is first loaded into it from RDRAM; palettes live in its upper "
                       "half."},
    {"tile descriptor", "One of eight settings that say how to read a texture out of texture "
                        "memory: its format, where it starts, its row length, and how it "
                        "wraps, mirrors or clamps. Loads go through tile 7; drawing reads "
                        "from the tile gsSPTexture names, and the one after it."},
    {"cycle", "The RDP works on a pixel in one or two cycles. Two cycles let the combiner and "
              "the blender work twice: for two textures, or fog on top of a blend."},
    {"combiner", "The colour combiner makes a pixel's colour and alpha from its inputs, the "
                 "textures, the vertices' shade and the set colours, as (a − b) × c + d."},
    {"blender", "The blender mixes the combined pixel with the colour already in the frame "
                "buffer, or with fog, by two weights: how translucency and fog are drawn."},
    {"other mode", "Two words of RDP settings: the high one for texturing and the cycle type, "
                   "the low one for blending, depth and antialiasing."},
    {"geometry mode", "The RSP's settings for the triangles it sends: depth testing, shading, "
                      "lighting, fog, culling and texture coordinate generation."},
    {"segment", "Addresses in display lists are segmented: the top byte picks one of 16 "
                "segments, the rest is an offset. gsSPSegment points a segment at memory, so "
                "the same list can draw from wherever its data was loaded."},
    {"vertex buffer", "The RSP holds 32 transformed vertices at a time. gsSPVertex loads "
                      "them, and triangle commands name their corners by slot."},
}};

[[nodiscard]] std::string makeEquation(const std::array<n64::CombinerInput, 4>& inputs,
                                       const bool isAlpha) {
    std::array<std::string_view, 4> names{};
    for (usize i = 0; i < inputs.size(); ++i) {
        names[i] = n64::getCombinerInputName(inputs[i], isAlpha);
    }
    std::string reduced;
    if (names[2] == "0") {
        reduced = names[3];
    } else {
        reduced =
            names[1] == "0" ? std::string(names[0]) : std::format("({} − {})", names[0], names[1]);
        if (names[2] != "1") {
            reduced += std::format(" × {}", names[2]);
        }
        if (names[3] != "0") {
            reduced += std::format(" + {}", names[3]);
        }
    }
    return std::format("({} − {}) × {} + {}  =  {}", names[0], names[1], names[2], names[3],
                       reduced);
}

[[nodiscard]] std::string_view nameBlendColor(const n64::BlendColor color) {
    switch (color) {
    case n64::BlendColor::Pixel:
        return "the combined pixel";
    case n64::BlendColor::Memory:
        return "the frame buffer";
    case n64::BlendColor::Blend:
        return "the blend colour";
    case n64::BlendColor::Fog:
        return "the fog colour";
    }
    return "?";
}

[[nodiscard]] std::string_view nameBlendAlpha(const n64::BlendAlpha alpha) {
    switch (alpha) {
    case n64::BlendAlpha::PixelAlpha:
        return "the pixel's alpha";
    case n64::BlendAlpha::FogAlpha:
        return "the fog colour's alpha";
    case n64::BlendAlpha::ShadeAlpha:
        return "the shade's alpha";
    case n64::BlendAlpha::Zero:
        return "0";
    }
    return "?";
}

[[nodiscard]] std::string_view nameBlendWeight(const n64::BlendWeight weight) {
    switch (weight) {
    case n64::BlendWeight::InverseAlpha:
        return "1 − the first weight";
    case n64::BlendWeight::MemoryAlpha:
        return "the frame buffer's coverage";
    case n64::BlendWeight::One:
        return "1";
    case n64::BlendWeight::Zero:
        return "0";
    }
    return "?";
}

[[nodiscard]] std::string makeColorText(const n64::Color& color) {
    return std::format("{}, {}, {}, {}", color[0], color[1], color[2], color[3]);
}

[[nodiscard]] std::string makeYesNo(const bool value) {
    return value ? "yes" : "no";
}

[[nodiscard]] StateSection makeCombinerSection(const n64::RdpState& state) {
    StateSection section{.title = "Colour combiner", .term = "combiner"};
    const n64::CycleType cycle = state.otherMode.getCycleType();
    if (cycle == n64::CycleType::Copy || cycle == n64::CycleType::Fill) {
        section.lines.push_back(
            StateLine{.label = "unused",
                      .value = cycle == n64::CycleType::Copy ? "copy mode" : "fill mode",
                      .description = "Copy mode draws texels as they are and fill mode a colour; "
                                     "neither combines."});
        return section;
    }
    const usize first = cycle == n64::CycleType::Two ? 0 : 1;
    for (usize i = first; i < state.combineMode.cycles.size(); ++i) {
        const n64::CombinerCycle& combined = state.combineMode.cycles[i];
        section.lines.push_back(
            StateLine{.label = std::format("cycle {} colour", i + 1),
                      .value = makeEquation(combined.color, false),
                      .description = "The colour, from (a − b) × c + d, and what it comes to."});
        section.lines.push_back(StateLine{
            .label = std::format("cycle {} alpha", i + 1),
            .value = makeEquation(combined.alpha, true),
            .description = "The alpha, the same way. In alpha slots every input is an alpha."});
    }
    return section;
}

[[nodiscard]] StateSection makeBlenderSection(const n64::RdpState& state) {
    StateSection section{.title = "Blender", .term = "blender"};
    const bool isTwo = state.otherMode.getCycleType() == n64::CycleType::Two;
    for (usize i = 0; i < (isTwo ? 2U : 1U); ++i) {
        const n64::BlenderCycle cycle = state.otherMode.getBlenderCycle(i);
        section.lines.push_back(StateLine{
            .label = std::format("cycle {}", i + 1),
            .value = std::format("{} × {}  +  {} × {}", nameBlendColor(cycle.first),
                                 nameBlendAlpha(cycle.firstWeight), nameBlendColor(cycle.second),
                                 nameBlendWeight(cycle.secondWeight)),
            .description = "The colour written: two colours, each by its weight, divided by the "
                           "weights' sum."});
    }
    section.lines.push_back(
        StateLine{.label = "reads the frame buffer",
                  .value = makeYesNo(state.otherMode.isImageRead()),
                  .description = "Whether the blender reads what is there, which translucent "
                                 "surfaces need."});
    section.lines.push_back(StateLine{.label = "always blends",
                                      .value = makeYesNo(state.otherMode.isForceBlend()),
                                      .description = "Whether every pixel blends, or only the "
                                                     "edges antialiasing covers in part."});
    return section;
}

[[nodiscard]] StateSection makeModeSection(const n64::RdpState& state) {
    const n64::OtherMode& mode = state.otherMode;
    StateSection section{.title = "Other mode", .term = "other mode"};
    constexpr std::array<std::string_view, 4> Cycles{"one cycle", "two cycles", "copy", "fill"};
    section.lines.push_back({"cycle type",
                             std::string(Cycles[static_cast<usize>(mode.getCycleType()) & 3]),
                             n64::getOtherModeDescription(true, 20)});
    const n64::TextureFilter filter = mode.getTextureFilter();
    section.lines.push_back({"texture filter",
                             filter == n64::TextureFilter::Point      ? "point"
                             : filter == n64::TextureFilter::Bilinear ? "bilinear"
                                                                      : "average",
                             n64::getOtherModeDescription(true, 12)});
    section.lines.push_back({"perspective correct", makeYesNo(mode.isTexturePerspective()),
                             n64::getOtherModeDescription(true, 19)});
    section.lines.push_back({"level of detail", makeYesNo(mode.isTextureLod()),
                             n64::getOtherModeDescription(true, 16)});
    const n64::AlphaCompare compare = mode.getAlphaCompare();
    section.lines.push_back({"alpha compare",
                             compare == n64::AlphaCompare::None        ? "none"
                             : compare == n64::AlphaCompare::Threshold ? "against the blend alpha"
                                                                       : "dithered",
                             n64::getOtherModeDescription(false, 0)});
    section.lines.push_back({"depth compare", makeYesNo(mode.isDepthCompared()),
                             "Whether a pixel behind what is drawn already is dropped."});
    section.lines.push_back({"depth update", makeYesNo(mode.isDepthUpdated()),
                             "Whether a pixel drawn writes its depth."});
    constexpr std::array<std::string_view, 4> Depths{"opaque", "interpenetrating", "translucent",
                                                     "decal"};
    section.lines.push_back(
        {"depth mode", std::string(Depths[static_cast<usize>(mode.getDepthMode()) & 3]),
         "How depth is compared: decals pass at equal depth, so they draw on their surface."});
    section.lines.push_back({"antialiased", makeYesNo(mode.isAntiAliased()),
                             "Whether edges are smoothed by their coverage."});
    return section;
}

[[nodiscard]] StateSection makeColorSection(const n64::RdpState& state) {
    return StateSection{
        .title = "Colours",
        .term = "combiner",
        .lines = {{"primitive", makeColorText(state.primitive), "PRIMITIVE in the combiner."},
                  {"environment", makeColorText(state.environment), "ENVIRONMENT in the combiner."},
                  {"blend", makeColorText(state.blend), "The blender's blend colour."},
                  {"fog", makeColorText(state.fog), "The colour fog blends towards."}}};
}

[[nodiscard]] std::string makeAxisText(const n64::TileAxis& axis) {
    return std::format("{} to {}, mask {}, shift {}{}{}", axis.low / 4, axis.high / 4, axis.mask,
                       axis.shift, axis.mirror ? ", mirrored" : "", axis.clamp ? ", clamped" : "");
}

[[nodiscard]] StateSection makeTileSection(const n64::RdpState& state) {
    StateSection section{.title = "Tiles", .term = "tile descriptor"};
    for (usize i = 0; i < state.tiles.size(); ++i) {
        const n64::TileDescriptor& tile = state.tiles[i];
        if (tile.line == 0 && tile.axes[0].high == 0) {
            continue;
        }
        const auto format = static_cast<usize>(tile.format);
        const auto size = static_cast<usize>(tile.size);
        section.lines.push_back(StateLine{
            .label = std::format("tile {}", i),
            .value = std::format("{}{} at 0x{:03X}, rows of {} words, palette {}; s {}; t {}",
                                 format < FormatNames.size() ? FormatNames[format] : "?",
                                 size < SizeNames.size() ? SizeNames[size] : "?", tile.address * 8U,
                                 tile.line, tile.palette, makeAxisText(tile.axes[0]),
                                 makeAxisText(tile.axes[1])),
            .description = findGlossaryEntry("tile descriptor")});
    }
    return section;
}

} // namespace

std::vector<StateSection> describeRdpState(const n64::RdpState& state) {
    return {makeCombinerSection(state), makeBlenderSection(state), makeModeSection(state),
            makeColorSection(state), makeTileSection(state)};
}

std::optional<TilePreview> makeTilePreview(const n64::RdpState& state,
                                           const n64::TextureMemoryImage& memory,
                                           const usize tile) {
    if (tile >= state.tiles.size()) {
        return std::nullopt;
    }
    const n64::TileDescriptor& descriptor = state.tiles[tile];
    if (descriptor.line == 0) {
        return std::nullopt;
    }
    const auto measure = [](const n64::TileAxis& axis) {
        return std::min<u32>(((axis.high >= axis.low ? axis.high - axis.low : 0U) / 4U) + 1U,
                             PreviewLimit);
    };
    TilePreview preview{.width = measure(descriptor.axes[0]),
                        .height = measure(descriptor.axes[1])};
    preview.pixels.reserve(static_cast<usize>(preview.width) * preview.height * 4);
    const n64::TlutMode tlut = state.otherMode.getTlutMode();
    for (u32 y = 0; y < preview.height; ++y) {
        for (u32 x = 0; x < preview.width; ++x) {
            const n64::Color texel =
                n64::fetchTexel(std::span<const u8, n64::TextureMemorySize>(memory), descriptor,
                                {static_cast<s32>(x), static_cast<s32>(y)}, tlut);
            preview.pixels.insert(preview.pixels.end(), texel.begin(), texel.end());
        }
    }
    return preview;
}

std::string_view findGlossaryEntry(const std::string_view term) {
    const auto found = std::ranges::find(Glossary, term, &Term::name);
    return found == Glossary.end() ? std::string_view() : found->meaning;
}

} // namespace toki::displayList
