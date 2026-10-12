#pragma once

#include "n64/CommandDecoder.h"
#include "n64/Types.h"

#include <optional>
#include <string_view>
#include <utility>

namespace n64::detail {

/// The `width` bits of `word` from bit `shift` up.
[[nodiscard]] constexpr u32 readBits(const u32 word, const u32 shift, const u32 width) {
    return (word >> shift) & ((1U << width) - 1U);
}

/// A command of `opcode` under `name`, to add fields to.
[[nodiscard]] inline DecodedCommand makeCommand(const u32 word0, const std::string_view name,
                                                const CommandKind kind,
                                                const std::string_view description) {
    return DecodedCommand{.opcode = static_cast<u8>(word0 >> 24),
                          .name = name,
                          .kind = kind,
                          .description = description};
}

inline void addField(DecodedCommand& command, const std::string_view name, const u32 value,
                     const FieldKind kind, const std::string_view description, const u8 shift = 0) {
    command.fields.push_back(CommandField{
        .name = name, .value = value, .kind = kind, .description = description, .shift = shift});
}

/// The commands the RDP runs, which F3DEX2 passes on to it, or nothing for
/// any other opcode. Private to `decodeCommand`.
[[nodiscard]] std::optional<DecodedCommand> decodeRdpCommand(u32 word0, u32 word1);

} // namespace n64::detail
