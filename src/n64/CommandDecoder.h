#pragma once

#include "n64/CombineMode.h"
#include "n64/Types.h"

#include <string_view>
#include <vector>

namespace n64 {

/// What part of drawing a command takes part in, for telling commands apart at
/// a glance.
enum class CommandKind : u8 {
    Geometry,
    Texture,
    Material,
    RenderMode,
    Transform,
    Control,
    Sync,
    Other,
};

/// How a field's value reads.
enum class FieldKind : u8 {
    Count,
    Index,
    Hex,
    Address,
    FixedPoint,
    Fraction,
    Color,
    GeometryMode,
    OtherModeHigh,
    OtherModeLow,
    MatrixFlags,
    MoveWordIndex,
    MoveMemoryIndex,
    ImageFormat,
    TexelSize,
    TileWrap,
    CombinerInput,
    Flag,
};

/// One field of a command: its name, its value as the words hold it, how it
/// reads, and what it means. An other mode field also says which bits of the
/// mode word it sets, by the shift of its lowest bit. A fixed point value has
/// two fractional bits, a fraction sixteen.
struct CommandField {
    std::string_view name;
    u32 value = 0;
    FieldKind kind = FieldKind::Hex;
    std::string_view description;
    u8 shift = 0;
};

/// A command of a display list as F3DEX2 reads it: the `gbi.h` macro it is
/// written with, what kind of command it is, what it does, and its fields. An
/// opcode F3DEX2 does not have reads as an unknown command with its words.
struct DecodedCommand {
    u8 opcode = 0;
    std::string_view name;
    CommandKind kind = CommandKind::Other;
    std::string_view description;
    std::vector<CommandField> fields;
};

/// Decodes the command `word0`, `word1`.
[[nodiscard]] DecodedCommand decodeCommand(u32 word0, u32 word1);

/// What a combiner input is called, as `gbi.h` writes it: `TEXEL0`,
/// `SHADE_ALPHA`. In an alpha slot, where every input is an alpha, an alpha
/// input is called by its source alone, `SHADE`.
[[nodiscard]] std::string_view getCombinerInputName(CombinerInput input, bool isAlpha = false);

/// What a combiner input means, for a person learning the combiner.
[[nodiscard]] std::string_view getCombinerInputDescription(CombinerInput input);

/// What one of F3DEX2's geometry mode bits does, named by its bit, or nothing
/// for a bit F3DEX2 does not use.
[[nodiscard]] std::string_view getGeometryModeDescription(u32 bit);

/// What the other mode field whose lowest bit is `shift` sets, in the high
/// word or the low, or nothing for bits no field has.
[[nodiscard]] std::string_view getOtherModeDescription(bool isHigh, u8 shift);

} // namespace n64
