#include "CommandDecoder.h"

#include "n64/Opcode.h"
#include "n64/Types.h"
#include "n64/detail/CommandDecoding.h"

#include <optional>
#include <string_view>

namespace n64 {

namespace {

using detail::addField;
using detail::makeCommand;
using detail::readBits;

constexpr u32 MatrixPush = 0x01;
constexpr u32 LightSize = 24;
constexpr u32 MatrixSize = 64;

constexpr std::string_view SlotText =
    "A slot of the RSP's vertex buffer, which holds 32 vertices; triangles name their corners "
    "by slot.";

void addTriangle(DecodedCommand& command, const u32 indices, const std::string_view first,
                 const std::string_view second, const std::string_view third) {
    addField(command, first, readBits(indices, 16, 8) / 2, FieldKind::Index, SlotText);
    addField(command, second, readBits(indices, 8, 8) / 2, FieldKind::Index, SlotText);
    addField(command, third, readBits(indices, 0, 8) / 2, FieldKind::Index, SlotText);
}

[[nodiscard]] DecodedCommand decodeVertex(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsSPVertex", CommandKind::Geometry,
        "Loads vertices from memory into the RSP's vertex buffer. Each is transformed by the "
        "current matrices, lit and fogged as it arrives; the triangles drawn after it name "
        "their corners by the slots they were loaded into.");
    const u32 count = readBits(word0, 12, 8);
    addField(command, "address", word1, FieldKind::Address,
             "Where the vertices are read from, 16 bytes each: position, texture coordinates "
             "and a colour or a normal.");
    addField(command, "count", count, FieldKind::Count, "How many vertices are loaded.");
    addField(command, "first slot", readBits(word0, 1, 7) - count, FieldKind::Index,
             "The slot the first one goes into; the rest follow in order.");
    return command;
}

[[nodiscard]] DecodedCommand decodeGeometryMode(const u32 word0, const u32 word1) {
    const u32 kept = readBits(word0, 0, 24);
    const u32 cleared = ~kept & 0x00FF'FFFF;
    std::string_view name = "gsSPGeometryMode";
    if (kept == 0) {
        name = "gsSPLoadGeometryMode";
    } else if (cleared == 0) {
        name = "gsSPSetGeometryMode";
    } else if (word1 == 0) {
        name = "gsSPClearGeometryMode";
    }
    DecodedCommand command = makeCommand(
        word0, name, CommandKind::RenderMode,
        "Turns the RSP's geometry mode bits on and off: whether triangles are depth tested, "
        "shaded, lit, fogged, culled, and how texture coordinates are made.");
    if (name != "gsSPSetGeometryMode" && name != "gsSPLoadGeometryMode") {
        addField(command, "clear", cleared, FieldKind::GeometryMode, "The bits turned off.");
    }
    if (name != "gsSPClearGeometryMode") {
        addField(command, "set", word1, FieldKind::GeometryMode, "The bits turned on.");
    }
    return command;
}

[[nodiscard]] DecodedCommand decodeMatrix(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsSPMatrix", CommandKind::Transform,
        "Loads a 4×4 fixed-point matrix from memory and either replaces the current modelview "
        "or projection matrix with it or multiplies it on. The modelview places what is drawn "
        "in the world; the projection turns the world into the screen.");
    addField(command, "address", word1, FieldKind::Address,
             "Where the matrix is read from: 64 bytes, the integer halves then the fractions.");
    addField(command, "flags", readBits(word0, 0, 8) ^ MatrixPush, FieldKind::MatrixFlags,
             "Which matrix it sets, projection or modelview; whether it loads or multiplies; and "
             "whether the modelview is first pushed onto the stack, to be popped back later.");
    return command;
}

[[nodiscard]] DecodedCommand decodeMoveWord(const u32 word0, const u32 word1) {
    const u32 index = readBits(word0, 16, 8);
    const u32 offset = readBits(word0, 0, 16);
    switch (index) {
    case 0x06: {
        DecodedCommand command = makeCommand(
            word0, "gsSPSegment", CommandKind::Transform,
            "Points one of the 16 segments at a physical address, so every segmented address "
            "after it that names the segment reads from there. Scenes, rooms and objects are "
            "each drawn through their own segment.");
        addField(command, "segment", offset / 4, FieldKind::Index, "The segment pointed.");
        addField(command, "address", word1, FieldKind::Hex,
                 "Where it now points, as a physical address.");
        return command;
    }
    case 0x02: {
        DecodedCommand command = makeCommand(
            word0, "gsSPNumLights", CommandKind::Material,
            "Sets how many directional lights the lighting of vertices uses, besides the "
            "ambient light.");
        addField(command, "lights", word1 / LightSize, FieldKind::Count, "The count of lights.");
        return command;
    }
    case 0x08: {
        DecodedCommand command = makeCommand(
            word0, "gsSPFogFactor", CommandKind::Material,
            "Sets how fog grows with depth: each vertex's fog is its depth times the multiplier "
            "plus the offset.");
        addField(command, "multiplier", readBits(word1, 16, 16), FieldKind::Hex,
                 "How fast fog grows, as a signed number.");
        addField(command, "offset", readBits(word1, 0, 16), FieldKind::Hex,
                 "Where fog starts, as a signed number.");
        return command;
    }
    default:
        break;
    }
    DecodedCommand command = makeCommand(
        word0, "gsMoveWd", CommandKind::Other,
        "Writes one word into the RSP's own memory: a light's colour, the clip ratio, a "
        "matrix element and the like, picked by the index.");
    addField(command, "index", index, FieldKind::MoveWordIndex, "What is written.");
    addField(command, "offset", offset, FieldKind::Hex, "Where in it.");
    addField(command, "data", word1, FieldKind::Hex, "The word written.");
    return command;
}

[[nodiscard]] DecodedCommand decodeMoveMemory(const u32 word0, const u32 word1) {
    const u32 index = readBits(word0, 0, 8);
    const u32 offset = readBits(word0, 8, 8) * 8;
    if (index == 0x08) {
        DecodedCommand command = makeCommand(
            word0, "gsSPViewport", CommandKind::Transform,
            "Sets the viewport: the scale and translation that take clip space to the screen.");
        addField(command, "address", word1, FieldKind::Address, "Where the viewport is read from.");
        return command;
    }
    if (index == 0x0A) {
        DecodedCommand command = makeCommand(
            word0, "gsSPLight", CommandKind::Material,
            "Loads a light: its colour and direction. Lights 1 and up are directional, and the "
            "one after the last is the ambient light.");
        addField(command, "address", word1, FieldKind::Address, "Where the light is read from.");
        addField(command, "light", offset / LightSize - 1, FieldKind::Index, "Which light.");
        return command;
    }
    DecodedCommand command =
        makeCommand(word0, "gsDma2p", CommandKind::Other,
                    "Copies a block of memory into the RSP's own memory, picked by the index.");
    addField(command, "address", word1, FieldKind::Address, "Where it is read from.");
    addField(command, "length", (readBits(word0, 19, 5) + 1) * 8, FieldKind::Count,
             "How many bytes.");
    addField(command, "index", index, FieldKind::MoveMemoryIndex, "What is written.");
    addField(command, "offset", offset, FieldKind::Hex, "Where in it.");
    return command;
}

[[nodiscard]] std::string_view findOtherModeName(const bool isHigh, const u32 shift) {
    if (isHigh) {
        switch (shift) {
        case 4:
            return "gsDPSetAlphaDither";
        case 6:
            return "gsDPSetColorDither";
        case 8:
            return "gsDPSetCombineKey";
        case 9:
            return "gsDPSetTextureConvert";
        case 12:
            return "gsDPSetTextureFilter";
        case 14:
            return "gsDPSetTextureLUT";
        case 16:
            return "gsDPSetTextureLOD";
        case 17:
            return "gsDPSetTextureDetail";
        case 19:
            return "gsDPSetTexturePersp";
        case 20:
            return "gsDPSetCycleType";
        case 23:
            return "gsDPPipelineMode";
        default:
            return "gsSPSetOtherMode";
        }
    }
    switch (shift) {
    case 0:
        return "gsDPSetAlphaCompare";
    case 2:
        return "gsDPSetDepthSource";
    case 3:
        return "gsDPSetRenderMode";
    default:
        return "gsSPSetOtherMode";
    }
}

[[nodiscard]] DecodedCommand decodeOtherMode(const u32 word0, const u32 word1, const bool isHigh) {
    const u32 length = readBits(word0, 0, 8) + 1;
    const u32 shift = 32 - readBits(word0, 8, 8) - length;
    const std::string_view known = getOtherModeDescription(isHigh, static_cast<u8>(shift));
    const std::string_view description =
        known.empty() ? std::string_view("Sets bits of the RDP's other mode, the settings of its "
                                         "texturing and blending.")
                      : known;
    DecodedCommand command = makeCommand(word0, findOtherModeName(isHigh, shift),
                                         CommandKind::RenderMode, description);
    addField(command, "value", word1, isHigh ? FieldKind::OtherModeHigh : FieldKind::OtherModeLow,
             description, static_cast<u8>(shift));
    addField(command, "bits", length, FieldKind::Count, "How many bits of the mode it sets.");
    return command;
}

[[nodiscard]] DecodedCommand decodeTexture(const u32 word0, const u32 word1) {
    DecodedCommand command = makeCommand(
        word0, "gsSPTexture", CommandKind::Texture,
        "Turns texturing of the following triangles on or off, scales their texture "
        "coordinates, and picks the tile they are drawn with and how many levels of detail "
        "follow it.");
    addField(command, "s scale", readBits(word1, 16, 16), FieldKind::Fraction,
             "What the horizontal texture coordinates are multiplied by, as a fraction of "
             "0x10000; 0xFFFF keeps them as they are.");
    addField(command, "t scale", readBits(word1, 0, 16), FieldKind::Fraction,
             "What the vertical texture coordinates are multiplied by.");
    addField(command, "levels", readBits(word0, 11, 3), FieldKind::Count,
             "How many mipmap levels follow the tile, less one.");
    addField(command, "tile", readBits(word0, 8, 3), FieldKind::Index,
             "The tile descriptor the triangles are textured from.");
    addField(command, "on", readBits(word0, 1, 7), FieldKind::Flag,
             "Whether the triangles are textured at all.");
    return command;
}

[[nodiscard]] DecodedCommand decodeDisplayList(const u32 word0, const u32 word1) {
    const bool isBranch = readBits(word0, 16, 8) != 0;
    DecodedCommand command = makeCommand(
        word0, isBranch ? "gsSPBranchList" : "gsSPDisplayList", CommandKind::Control,
        isBranch ? std::string_view("Continues with another display list and does not come "
                                    "back: what follows this command is never run.")
                 : std::string_view("Runs another display list, then comes back to the "
                                    "command after this one, as a function call does. Calls "
                                    "nest up to 18 deep."));
    addField(command, "address", word1, FieldKind::Address, "The display list run.");
    return command;
}

[[nodiscard]] DecodedCommand decodeTextureRectangle(const u32 word0, const u32 word1,
                                                    const bool isFlipped) {
    DecodedCommand command = makeCommand(
        word0, isFlipped ? "gsSPTextureRectangleFlip" : "gsSPTextureRectangle",
        CommandKind::Geometry,
        "Draws a screen-aligned rectangle textured from a tile, as menus and text are drawn. "
        "Its texture coordinates follow in the two RDP half commands after it.");
    addField(command, "tile", readBits(word1, 24, 3), FieldKind::Index, "The tile it reads.");
    addField(command, "left", readBits(word1, 12, 12), FieldKind::FixedPoint,
             "Its left edge on the screen, in pixels.");
    addField(command, "top", readBits(word1, 0, 12), FieldKind::FixedPoint, "Its top edge.");
    addField(command, "right", readBits(word0, 12, 12), FieldKind::FixedPoint, "Its right edge.");
    addField(command, "bottom", readBits(word0, 0, 12), FieldKind::FixedPoint, "Its bottom edge.");
    return command;
}

[[nodiscard]] std::optional<DecodedCommand> decodeRspCommand(const u32 word0, const u32 word1) {
    switch (static_cast<Opcode>(word0 >> 24)) {
    case Opcode::NoOp:
        return makeCommand(word0, "gsDPNoOp", CommandKind::Other, "Does nothing.");
    case Opcode::Vertex:
        return decodeVertex(word0, word1);
    case Opcode::ModifyVertex: {
        DecodedCommand command =
            makeCommand(word0, "gsSPModifyVertex", CommandKind::Geometry,
                        "Changes one value of a vertex already in the buffer, its colour, texture "
                        "coordinates or screen position, without loading it again.");
        addField(command, "slot", readBits(word0, 0, 16) / 2, FieldKind::Index, SlotText);
        addField(command, "where", readBits(word0, 16, 8), FieldKind::Hex,
                 "Which value: 0x10 colour, 0x14 texture coordinates, 0x18 screen position, "
                 "0x1C screen depth.");
        addField(command, "value", word1, FieldKind::Hex, "The new value.");
        return command;
    }
    case Opcode::CullDisplayList: {
        DecodedCommand command = makeCommand(
            word0, "gsSPCullDisplayList", CommandKind::Control,
            "Ends the display list early when every vertex of a range is off the screen on the "
            "same side, so nothing of an object out of view is drawn. The range is often a "
            "bounding box loaded for that.");
        addField(command, "first", readBits(word0, 0, 16) / 2, FieldKind::Index, SlotText);
        addField(command, "last", word1 / 2, FieldKind::Index, SlotText);
        return command;
    }
    case Opcode::BranchZ: {
        DecodedCommand command = makeCommand(
            word0, "gsSPBranchLessZraw", CommandKind::Control,
            "Branches to the display list the gsDPHalf1 before it names when a vertex is nearer "
            "than a depth, as a model swaps in its detailed version close up.");
        addField(command, "slot", readBits(word0, 0, 12) / 2, FieldKind::Index, SlotText);
        addField(command, "depth", word1, FieldKind::Hex, "The depth it is compared against.");
        return command;
    }
    case Opcode::Triangle1: {
        DecodedCommand command = makeCommand(
            word0, "gsSP1Triangle", CommandKind::Geometry,
            "Draws one triangle between three vertices of the buffer, unless it is culled for "
            "facing away.");
        addTriangle(command, word0, "a", "b", "c");
        return command;
    }
    case Opcode::Triangle2:
    case Opcode::Quad: {
        const bool isQuad = static_cast<Opcode>(word0 >> 24) == Opcode::Quad;
        DecodedCommand command = makeCommand(
            word0, isQuad ? "gsSP1Quadrangle" : "gsSP2Triangles", CommandKind::Geometry,
            "Draws two triangles between vertices of the buffer in one command, each unless it "
            "is culled for facing away.");
        addTriangle(command, word0, "a", "b", "c");
        addTriangle(command, word1, "d", "e", "f");
        return command;
    }
    case Opcode::Texture:
        return decodeTexture(word0, word1);
    case Opcode::PopMatrix: {
        DecodedCommand command = makeCommand(
            word0, "gsSPPopMatrix", CommandKind::Transform,
            "Takes modelview matrices pushed earlier back off the stack, restoring the one "
            "before them.");
        addField(command, "count", word1 / MatrixSize, FieldKind::Count, "How many are popped.");
        return command;
    }
    case Opcode::GeometryMode:
        return decodeGeometryMode(word0, word1);
    case Opcode::Matrix:
        return decodeMatrix(word0, word1);
    case Opcode::MoveWord:
        return decodeMoveWord(word0, word1);
    case Opcode::MoveMemory:
        return decodeMoveMemory(word0, word1);
    case Opcode::DisplayList:
        return decodeDisplayList(word0, word1);
    case Opcode::EndDisplayList:
        return makeCommand(word0, "gsSPEndDisplayList", CommandKind::Control,
                           "Ends the display list: a called one returns to its caller, the "
                           "first one ends the frame's drawing.");
    case Opcode::SpNoOp:
        return makeCommand(word0, "gsSPNoOp", CommandKind::Other, "Does nothing.");
    case Opcode::RdpHalf1: {
        DecodedCommand command = makeCommand(
            word0, "gsDPHalf1", CommandKind::Other,
            "Holds a word for the command after it: the target of gsSPBranchLessZraw, or a "
            "texture rectangle's coordinates.");
        addField(command, "word", word1, FieldKind::Hex, "The word held.");
        return command;
    }
    case Opcode::SetOtherModeLow:
        return decodeOtherMode(word0, word1, false);
    case Opcode::SetOtherModeHigh:
        return decodeOtherMode(word0, word1, true);
    case Opcode::TextureRectangle:
        return decodeTextureRectangle(word0, word1, false);
    case Opcode::TextureRectangleFlip:
        return decodeTextureRectangle(word0, word1, true);
    case Opcode::LoadMicrocode:
        return makeCommand(word0, "gsSPLoadUcode", CommandKind::Control,
                           "Loads other microcode into the RSP. Toki runs F3DEX2 only.");
    case Opcode::DmaIo:
    case Opcode::Special1:
    case Opcode::Special2:
    case Opcode::Special3:
        return makeCommand(word0, "gsSPSpecial", CommandKind::Other,
                           "A command F3DEX2 keeps for debugging; it draws nothing.");
    default:
        return std::nullopt;
    }
}

} // namespace

DecodedCommand decodeCommand(const u32 word0, const u32 word1) {
    if (std::optional<DecodedCommand> command = decodeRspCommand(word0, word1)) {
        return *command;
    }
    if (std::optional<DecodedCommand> command = detail::decodeRdpCommand(word0, word1)) {
        return *command;
    }
    DecodedCommand command = makeCommand(word0, "unknown command", CommandKind::Other,
                                         "An opcode F3DEX2 does not have.");
    addField(command, "word 0", word0, FieldKind::Hex, "The first word.");
    addField(command, "word 1", word1, FieldKind::Hex, "The second word.");
    return command;
}

} // namespace n64
