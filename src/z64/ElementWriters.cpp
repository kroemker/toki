#include "ElementWriters.h"

#include "cparser/CType.h"
#include "n64/CommandDecoder.h"
#include "z64/GbiNames.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <format>
#include <optional>
#include <span>
#include <string>

namespace z64 {

namespace {

constexpr usize VertexSize = 0x10;
constexpr usize CommandSize = 8;
constexpr usize WordSize = 4;

u32 readWord(const std::span<const u8> bytes, const usize offset) {
    u32 word = 0;
    for (usize i = 0; i < WordSize; ++i) {
        word = (word << 8) | bytes[offset + i];
    }
    return word;
}

s16 readSigned(const std::span<const u8> bytes, const usize offset) {
    return static_cast<s16>((bytes[offset] << 8) | bytes[offset + 1]);
}

const cparser::CType& getPointerType() {
    static const cparser::CType VoidType{.kind = cparser::TypeKind::Void};
    static const cparser::CType PointerType{
        .kind = cparser::TypeKind::Pointer, .size = 4, .alignment = 4, .element = &VoidType};
    return PointerType;
}

} // namespace

ElementWriter makeVertexWriter() {
    return [](const DeclarationWriter&, const ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != VertexSize || !element.relocations.empty()) {
            return std::nullopt;
        }
        const u16 flag = static_cast<u16>((bytes[6] << 8) | bytes[7]);
        return std::format("{{{{ {{{}, {}, {}}}, {}, {{{}, {}}}, {{0x{:02X}, 0x{:02X}, 0x{:02X}, "
                           "0x{:02X}}} }}}}",
                           readSigned(bytes, 0), readSigned(bytes, 2), readSigned(bytes, 4), flag,
                           readSigned(bytes, 8), readSigned(bytes, 10), bytes[12], bytes[13],
                           bytes[14], bytes[15]);
    };
}

ElementWriter makeDisplayListWriter(const GbiNames& names) {
    return [&names](const DeclarationWriter& writer,
                    const ElementData& element) -> std::optional<std::string> {
        if (element.bytes.size() != CommandSize || element.findRelocation(0) != nullptr) {
            return std::nullopt;
        }
        const u32 word1 = readWord(element.bytes, WordSize);
        const Relocation* relocation = element.findRelocation(WordSize);
        const FindAddressName findAddressName = [&](u32) -> std::optional<std::string> {
            if (relocation == nullptr) {
                return std::nullopt;
            }
            return writer.writeAddress(*relocation, word1, getPointerType());
        };
        return names.makeCommandText(n64::decodeCommand(readWord(element.bytes, 0), word1),
                                     findAddressName);
    };
}

} // namespace z64
