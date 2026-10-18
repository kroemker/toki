#include "ActorEntryWriter.h"

#include "app/scene/Types.h"
#include "cparser/TypeTable.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace toki::scene {

namespace {

constexpr usize EntrySize = 0x10;

u16 readHalf(const std::span<const u8> bytes, const usize offset) {
    return static_cast<u16>((bytes[offset] << 8) | bytes[offset + 1]);
}

s16 readSigned(const std::span<const u8> bytes, const usize offset) {
    return static_cast<s16>(readHalf(bytes, offset));
}

std::string writeActorName(const cparser::TypeTable& types, const s16 id) {
    if (const cparser::Enumeration* actors = types.findEnumeration("ActorID")) {
        const auto found =
            std::ranges::find(actors->constants, s64{id}, &cparser::Enumerator::value);
        if (found != actors->constants.end()) {
            return found->name;
        }
    }
    return std::format("{}", id);
}

std::string writePosition(const std::span<const u8> bytes, const usize offset) {
    return std::format("{{ {}, {}, {} }}", readSigned(bytes, offset), readSigned(bytes, offset + 2),
                       readSigned(bytes, offset + 4));
}

std::string writeHex(const std::span<const u8> bytes, const usize offset) {
    return std::format("0x{:04X}", readHalf(bytes, offset));
}

} // namespace

z64::ElementWriter makeActorEntryWriter(const cparser::TypeTable& types) {
    return [&types](const z64::DeclarationWriter&,
                    const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != EntrySize || !element.relocations.empty()) {
            return std::nullopt;
        }
        return std::format("{{ {}, {}, {{ {}, {}, {} }}, {} }}",
                           writeActorName(types, readSigned(bytes, 0)), writePosition(bytes, 2),
                           writeHex(bytes, 8), writeHex(bytes, 10), writeHex(bytes, 12),
                           writeHex(bytes, 14));
    };
}

z64::ElementWriter makeTransitionActorEntryWriter(const cparser::TypeTable& types) {
    return [&types](const z64::DeclarationWriter&,
                    const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != EntrySize || !element.relocations.empty()) {
            return std::nullopt;
        }
        const auto readSide = [&bytes](const usize offset) {
            return std::format("{{ {}, {} }}", static_cast<s8>(bytes[offset]),
                               static_cast<s8>(bytes[offset + 1]));
        };
        return std::format("{{ {{ {}, {} }}, {}, {}, {}, {} }}", readSide(0), readSide(2),
                           writeActorName(types, readSigned(bytes, 4)), writePosition(bytes, 6),
                           writeHex(bytes, 12), writeHex(bytes, 14));
    };
}

} // namespace toki::scene
