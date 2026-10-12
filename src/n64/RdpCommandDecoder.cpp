#include "n64/CombineMode.h"
#include "n64/CommandDecoder.h"
#include "n64/Opcode.h"
#include "n64/Types.h"
#include "n64/detail/CommandDecoding.h"

#include <array>
#include <optional>
#include <string_view>

namespace n64::detail {

namespace {

constexpr std::string_view TileText =
    "One of the RDP's eight tile descriptors, which say where in texture memory a texture "
    "lies and how it is read. Tile 7 is the one loads go through; drawing reads from 0 up.";
constexpr std::string_view FixedText = "In pixels or texels, with two fractional bits.";

[[nodiscard]] DecodedCommand decodeSync(const u32 word0, const std::string_view name,
                                        const std::string_view description) {
    return makeCommand(word0, name, CommandKind::Sync, description);
}

void addTileRange(DecodedCommand& command, const u32 word0, const u32 word1,
                  const std::string_view lastS, const std::string_view lastT) {
    addField(command, "tile", readBits(word1, 24, 3), FieldKind::Index, TileText);
    addField(command, "first s", readBits(word0, 12, 12), FieldKind::FixedPoint, FixedText);
    addField(command, "first t", readBits(word0, 0, 12), FieldKind::FixedPoint, FixedText);
    addField(command, lastS, readBits(word1, 12, 12), FieldKind::FixedPoint, FixedText);
    addField(command, lastT, readBits(word1, 0, 12), FieldKind::FixedPoint, FixedText);
}

[[nodiscard]] DecodedCommand decodeSetTile(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsDPSetTile", CommandKind::Texture,
        "Sets up a tile descriptor: the format of the texels it reads, where they start in "
        "texture memory and how long a row is, which palette, and how coordinates past its "
        "edges wrap, mirror or clamp.");
    addField(command, "tile", readBits(word1, 24, 3), FieldKind::Index, TileText);
    addField(command, "format", readBits(word0, 21, 3), FieldKind::ImageFormat,
             "What a texel holds: colour and alpha (RGBA), intensity (I), intensity and alpha "
             "(IA), or a palette index (CI).");
    addField(command, "size", readBits(word0, 19, 2), FieldKind::TexelSize,
             "How many bits a texel takes: 4, 8, 16 or 32.");
    addField(command, "line", readBits(word0, 9, 9), FieldKind::Count,
             "How long a row of the texture is in texture memory, in 64-bit words.");
    addField(command, "address", readBits(word0, 0, 9), FieldKind::Hex,
             "Where the texture starts in texture memory, in 64-bit words.");
    addField(command, "palette", readBits(word1, 20, 4), FieldKind::Index,
             "For a 4-bit palette texture, which of the 16 palettes it uses.");
    addField(command, "t wrap", readBits(word1, 18, 2), FieldKind::TileWrap,
             "Whether vertical coordinates past the edge mirror, clamp, or wrap.");
    addField(command, "t mask", readBits(word1, 14, 4), FieldKind::Count,
             "Vertical coordinates wrap every 2 to this power texels; 0 means they do not.");
    addField(command, "t shift", readBits(word1, 10, 4), FieldKind::Count,
             "Scales vertical coordinates down by 2 to this power, or up from 11 on, for "
             "detail and mipmap levels.");
    addField(command, "s wrap", readBits(word1, 8, 2), FieldKind::TileWrap,
             "As t wrap, horizontally.");
    addField(command, "s mask", readBits(word1, 4, 4), FieldKind::Count,
             "As t mask, horizontally.");
    addField(command, "s shift", readBits(word1, 0, 4), FieldKind::Count,
             "As t shift, horizontally.");
    return command;
}

[[nodiscard]] DecodedCommand decodeLoadBlock(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsDPLoadBlock", CommandKind::Texture,
        "Copies a run of texels from the texture image into texture memory in one go, as most "
        "textures are loaded. The tile says where they go; the step lets the RDP lay the rows "
        "out as the tiles that read them want.");
    addField(command, "tile", readBits(word1, 24, 3), FieldKind::Index, TileText);
    addField(command, "first s", readBits(word0, 12, 12), FieldKind::Count,
             "The first texel copied.");
    addField(command, "first t", readBits(word0, 0, 12), FieldKind::Count, "Its row.");
    addField(command, "last texel", readBits(word1, 12, 12), FieldKind::Count,
             "The last texel copied, counting from zero.");
    addField(command, "t step", readBits(word1, 0, 12), FieldKind::Hex,
             "How much a row advances per 64-bit word, as a fraction of 0x800.");
    return command;
}

[[nodiscard]] DecodedCommand decodeImage(const u32 word0, const u32 word1,
                                         const std::string_view name, const CommandKind kind,
                                         const std::string_view description) {
    DecodedCommand command = makeCommand(word0, name, kind, description);
    addField(command, "format", readBits(word0, 21, 3), FieldKind::ImageFormat,
             "What a texel holds.");
    addField(command, "size", readBits(word0, 19, 2), FieldKind::TexelSize,
             "How many bits a texel takes.");
    addField(command, "width", readBits(word0, 0, 12) + 1, FieldKind::Count,
             "How many texels a row of the image has.");
    addField(command, "address", word1, FieldKind::Address, "Where the image is.");
    return command;
}

[[nodiscard]] DecodedCommand decodeColor(const u32 word0, const u32 word1,
                                         const std::string_view name,
                                         const std::string_view description) {
    DecodedCommand command = makeCommand(word0, name, CommandKind::Material, description);
    addField(command, "color", word1, FieldKind::Color, "Red, green, blue and alpha, 0 to 255.");
    return command;
}

[[nodiscard]] DecodedCommand decodeCombine(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsDPSetCombineMode", CommandKind::Material,
        "Sets the colour combiner: how a pixel's colour and alpha are made from the texels, "
        "the shade of the vertices, the primitive and environment colours and more, as "
        "(a − b) × c + d, once for each of the two cycles.");
    const CombineMode mode = makeCombineMode(word0, word1);
    constexpr std::array<std::string_view, 4> Colors{"color a", "color b", "color c", "color d"};
    constexpr std::array<std::string_view, 4> Alphas{"alpha a", "alpha b", "alpha c", "alpha d"};
    for (usize cycle = 0; cycle < mode.cycles.size(); ++cycle) {
        for (usize input = 0; input < Colors.size(); ++input) {
            addField(command, Colors[input], static_cast<u32>(mode.cycles[cycle].color[input]),
                     FieldKind::CombinerInput,
                     cycle == 0 ? "Cycle 1's colour, which one-cycle mode does not use."
                                : "Cycle 2's colour, the one one-cycle mode combines with.",
                     static_cast<u8>(cycle));
        }
        for (usize input = 0; input < Alphas.size(); ++input) {
            addField(command, Alphas[input], static_cast<u32>(mode.cycles[cycle].alpha[input]),
                     FieldKind::CombinerInput, cycle == 0 ? "Cycle 1's alpha." : "Cycle 2's alpha.",
                     static_cast<u8>(cycle));
        }
    }
    return command;
}

[[nodiscard]] std::optional<DecodedCommand> decodeTextureCommand(const u32 word0, const u32 word1) {
    switch (static_cast<Opcode>(word0 >> 24)) {
    case Opcode::LoadTlut: {
        DecodedCommand command = makeCommand(
            word0, "gsDPLoadTLUTCmd", CommandKind::Texture,
            "Copies a palette from the texture image into the upper half of texture memory, "
            "where palette textures look their colours up.");
        addField(command, "tile", readBits(word1, 24, 3), FieldKind::Index, TileText);
        addField(command, "colors", readBits(word1, 14, 10) + 1, FieldKind::Count,
                 "How many colours the palette has.");
        return command;
    }
    case Opcode::SetTileSize: {
        DecodedCommand command = makeCommand(
            word0, "gsDPSetTileSize", CommandKind::Texture,
            "Sets a tile's extent: the texels it covers, which wrapping and clamping are "
            "measured from. Scrolling textures change it every frame.");
        addTileRange(command, word0, word1, "last s", "last t");
        return command;
    }
    case Opcode::LoadBlock:
        return decodeLoadBlock(word0, word1);
    case Opcode::LoadTile: {
        DecodedCommand command = makeCommand(
            word0, "gsDPLoadTile", CommandKind::Texture,
            "Copies a rectangle of the texture image into texture memory row by row, as part "
            "of a bigger image is loaded.");
        addTileRange(command, word0, word1, "last s", "last t");
        return command;
    }
    case Opcode::SetTile:
        return decodeSetTile(word0, word1);
    case Opcode::SetTextureImage:
        return decodeImage(word0, word1, "gsDPSetTextureImage", CommandKind::Texture,
                           "Says where the next texture or palette load reads from, and its "
                           "format. Nothing is copied until the load.");
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<DecodedCommand> decodeRdpCommand(const u32 word0, const u32 word1) {
    if (std::optional<DecodedCommand> command = decodeTextureCommand(word0, word1)) {
        return command;
    }
    switch (static_cast<Opcode>(word0 >> 24)) {
    case Opcode::LoadSync:
        return decodeSync(word0, "gsDPLoadSync",
                          "Waits until the RDP has drawn with texture memory before a load "
                          "overwrites it.");
    case Opcode::PipeSync:
        return decodeSync(word0, "gsDPPipeSync",
                          "Waits until the RDP has drawn everything before it, so a change of "
                          "mode or colour after it does not reach a primitive still drawing.");
    case Opcode::TileSync:
        return decodeSync(word0, "gsDPTileSync",
                          "Waits until no primitive still reads a tile descriptor before one "
                          "is changed.");
    case Opcode::FullSync:
        return decodeSync(word0, "gsDPFullSync",
                          "Waits until the RDP is done with everything, at the end of a frame.");
    case Opcode::SetKeyGreenBlue:
    case Opcode::SetKeyRed:
        return makeCommand(word0, "gsDPSetKey", CommandKind::Material,
                           "Sets the chroma key, the colour the combiner can key out.");
    case Opcode::SetConvert:
        return makeCommand(word0, "gsDPSetConvert", CommandKind::Material,
                           "Sets the constants for converting YUV texels to RGB.");
    case Opcode::SetScissor: {
        DecodedCommand command =
            makeCommand(word0, "gsDPSetScissor", CommandKind::RenderMode,
                        "Sets the scissor: the rectangle of the screen drawing is kept inside.");
        addField(command, "left", readBits(word0, 12, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "top", readBits(word0, 0, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "right", readBits(word1, 12, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "bottom", readBits(word1, 0, 12), FieldKind::FixedPoint, FixedText);
        return command;
    }
    case Opcode::SetPrimitiveDepth: {
        DecodedCommand command = makeCommand(
            word0, "gsDPSetPrimDepth", CommandKind::RenderMode,
            "Sets the depth primitives are drawn at when the other mode takes depth from the "
            "primitive rather than from each pixel, as for flat decals.");
        addField(command, "depth", readBits(word1, 16, 16), FieldKind::Hex, "The depth.");
        addField(command, "delta", readBits(word1, 0, 16), FieldKind::Hex, "Its slope.");
        return command;
    }
    case Opcode::SetOtherMode: {
        DecodedCommand command = makeCommand(
            word0, "gsDPSetOtherMode", CommandKind::RenderMode,
            "Sets all of the RDP's other mode at once: the high word of texturing and cycle "
            "settings and the low word of blending and depth.");
        addField(command, "high", readBits(word0, 0, 24), FieldKind::OtherModeHigh,
                 "The high word.");
        addField(command, "low", word1, FieldKind::OtherModeLow, "The low word.");
        return command;
    }
    case Opcode::RdpHalf2: {
        DecodedCommand command = makeCommand(word0, "gsDPHalf2", CommandKind::Other,
                                             "Holds the second word of a texture rectangle's "
                                             "coordinates.");
        addField(command, "word", word1, FieldKind::Hex, "The word held.");
        return command;
    }
    case Opcode::FillRectangle: {
        DecodedCommand command = makeCommand(
            word0, "gsDPFillRectangle", CommandKind::Geometry,
            "Fills a rectangle of the screen with the fill colour, as the frame and depth "
            "buffers are cleared.");
        addField(command, "left", readBits(word1, 12, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "top", readBits(word1, 0, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "right", readBits(word0, 12, 12), FieldKind::FixedPoint, FixedText);
        addField(command, "bottom", readBits(word0, 0, 12), FieldKind::FixedPoint, FixedText);
        return command;
    }
    case Opcode::SetFillColor: {
        DecodedCommand command = makeCommand(word0, "gsDPSetFillColor", CommandKind::Material,
                                             "Sets the colour fill rectangles are filled with, "
                                             "as packed pixels of the colour image.");
        addField(command, "color", word1, FieldKind::Hex, "Two 16-bit pixels, or one 32-bit.");
        return command;
    }
    case Opcode::SetFogColor:
        return decodeColor(word0, word1, "gsDPSetFogColor",
                           "Sets the colour fog blends towards, which the blender mixes in by "
                           "each pixel's fog.");
    case Opcode::SetBlendColor:
        return decodeColor(word0, word1, "gsDPSetBlendColor",
                           "Sets the blend colour the blender can mix in, and whose alpha the "
                           "alpha compare tests against.");
    case Opcode::SetPrimitiveColor: {
        DecodedCommand command = decodeColor(
            word0, word1, "gsDPSetPrimColor",
            "Sets the primitive colour, which the combiner can use as an input; tinting and "
            "fading models is mostly done with it.");
        addField(command, "min level", readBits(word0, 8, 8), FieldKind::Count,
                 "The lowest mipmap level used.");
        addField(command, "lod fraction", readBits(word0, 0, 8), FieldKind::Count,
                 "A level of detail fraction the combiner can use, 0 to 255.");
        return command;
    }
    case Opcode::SetEnvironmentColor:
        return decodeColor(word0, word1, "gsDPSetEnvColor",
                           "Sets the environment colour, a second colour the combiner can use "
                           "as an input, often for a model's tint or glow.");
    case Opcode::SetCombine:
        return decodeCombine(word0, word1);
    case Opcode::SetDepthImage: {
        DecodedCommand command =
            makeCommand(word0, "gsDPSetDepthImage", CommandKind::RenderMode,
                        "Says where the depth buffer is, which depth testing reads and writes.");
        addField(command, "address", word1, FieldKind::Address, "Where the depth buffer is.");
        return command;
    }
    case Opcode::SetColorImage:
        return decodeImage(word0, word1, "gsDPSetColorImage", CommandKind::RenderMode,
                           "Says where the RDP draws to: the frame buffer, or another image, as "
                           "when the depth buffer is cleared by drawing to it.");
    default:
        return std::nullopt;
    }
}

} // namespace n64::detail
