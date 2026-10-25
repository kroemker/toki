#include "CollisionWriters.h"

#include "app/scene/Types.h"
#include "cparser/CType.h"
#include "cparser/TypeTable.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::scene {

namespace {

constexpr usize PolygonSize = 0x10;
constexpr usize SurfaceTypeSize = 8;
constexpr usize WaterBoxSize = 0x10;
constexpr usize CameraSize = 8;
constexpr usize CameraDataOffset = 4;
constexpr f64 NormalScale = 32767.0;
constexpr int MostNormalDigits = 9;

u16 readHalf(const std::span<const u8> bytes, const usize offset) {
    return static_cast<u16>((bytes[offset] << 8) | bytes[offset + 1]);
}

s16 readSigned(const std::span<const u8> bytes, const usize offset) {
    return static_cast<s16>(readHalf(bytes, offset));
}

u32 readWord(const std::span<const u8> bytes, const usize offset) {
    return (u32{bytes[offset]} << 24) | (u32{bytes[offset + 1]} << 16) |
           (u32{bytes[offset + 2]} << 8) | u32{bytes[offset + 3]};
}

u32 readBits(const u32 word, const u32 shift, const u32 mask) {
    return (word >> shift) & mask;
}

std::string writeFlags(const u32 flags, const std::span<const std::string_view> names,
                       const std::string_view none) {
    std::string text;
    for (usize bit = 0; bit < names.size(); ++bit) {
        if ((flags & (1U << bit)) != 0) {
            text += text.empty() ? std::string(names[bit]) : std::format(" | {}", names[bit]);
        }
    }
    return text.empty() ? std::string(none) : text;
}

constexpr std::array<std::string_view, 3> IgnoreFlags{
    "COLPOLY_IGNORE_CAMERA", "COLPOLY_IGNORE_ENTITY", "COLPOLY_IGNORE_PROJECTILES"};

std::string writeVertex(const u16 data, const bool isFirst) {
    const u32 flags = data >> 13;
    if (isFirst) {
        return std::format("COLPOLY_VTX({}, {})", data & 0x1FFF,
                           writeFlags(flags, IgnoreFlags, "COLPOLY_IGNORE_NONE"));
    }
    if (flags == 0) {
        return std::format("COLPOLY_VTX({}, COLPOLY_IGNORE_NONE)", data & 0x1FFF);
    }
    if (flags == 1) {
        return std::format("COLPOLY_VTX({}, COLPOLY_IS_FLOOR_CONVEYOR)", data & 0x1FFF);
    }
    return std::format("COLPOLY_VTX({}, {})", data & 0x1FFF, flags);
}

/// Whether `text`, read as a double, truncates to `value` once scaled as
/// `COLPOLY_SNORMAL` scales it.
bool isNormalOf(const std::string& text, const s16 value) {
    f64 read = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), read);
    return error == std::errc{} && std::trunc(read * NormalScale) == static_cast<f64>(value);
}

std::string writeNormal(const s16 value) {
    if (value == 0) {
        return "COLPOLY_SNORMAL(0.0)";
    }
    const f64 middle =
        (static_cast<f64>(value) + std::copysign(0.5, static_cast<f64>(value))) / NormalScale;
    for (int digits = 1; digits <= MostNormalDigits; ++digits) {
        std::string text = std::format("{:.{}f}", middle, digits);
        while (text.ends_with('0') && !text.ends_with(".0")) {
            text.pop_back();
        }
        if (isNormalOf(text, value)) {
            return std::format("COLPOLY_SNORMAL({})", text);
        }
    }
    return std::format("{}", value);
}

} // namespace

z64::ElementWriter makeCollisionPolyWriter() {
    return [](const z64::DeclarationWriter&,
              const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != PolygonSize || !element.relocations.empty()) {
            return std::nullopt;
        }
        return std::format("{{ {}, {}, {}, COLPOLY_VTX_INDEX({}), {{ {}, {}, {} }}, {} }}",
                           readHalf(bytes, 0), writeVertex(readHalf(bytes, 2), true),
                           writeVertex(readHalf(bytes, 4), false), readHalf(bytes, 6),
                           writeNormal(readSigned(bytes, 8)), writeNormal(readSigned(bytes, 10)),
                           writeNormal(readSigned(bytes, 12)), readSigned(bytes, 14));
    };
}

z64::ElementWriter makeSurfaceTypeWriter() {
    return [](const z64::DeclarationWriter&,
              const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != SurfaceTypeSize || !element.relocations.empty()) {
            return std::nullopt;
        }
        const u32 first = readWord(bytes, 0);
        const u32 second = readWord(bytes, 4);
        return std::format(
            "{{ SURFACETYPE0({}, {}, {}, {}, {}, {}, {}, {}), "
            "SURFACETYPE1({}, {}, {}, {}, {}, {}, {}, {}) }}",
            readBits(first, 0, 0xFF), readBits(first, 8, 0x1F), readBits(first, 13, 0x1F),
            readBits(first, 18, 0x07), readBits(first, 21, 0x1F), readBits(first, 26, 0x0F),
            readBits(first, 30, 1), readBits(first, 31, 1), readBits(second, 0, 0x0F),
            readBits(second, 4, 0x03), readBits(second, 6, 0x1F), readBits(second, 11, 0x3F),
            readBits(second, 17, 1), readBits(second, 18, 0x07), readBits(second, 21, 0x3F),
            readBits(second, 27, 1));
    };
}

z64::ElementWriter makeWaterBoxWriter() {
    return [](const z64::DeclarationWriter&,
              const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != WaterBoxSize || !element.relocations.empty()) {
            return std::nullopt;
        }
        const u32 properties = readWord(bytes, 12);
        return std::format("{{ {}, {}, {}, {}, {}, WATERBOX_PROPERTIES({}, {}, {}, {}) }}",
                           readSigned(bytes, 0), readSigned(bytes, 2), readSigned(bytes, 4),
                           readSigned(bytes, 6), readSigned(bytes, 8),
                           readBits(properties, 0, 0xFF), readBits(properties, 8, 0x1F),
                           readBits(properties, 13, 0x3F), readBits(properties, 19, 1));
    };
}

z64::ElementWriter makeBgCamInfoWriter(const cparser::TypeTable& types) {
    return [&types](const z64::DeclarationWriter& writer,
                    const z64::ElementData& element) -> std::optional<std::string> {
        const std::span<const u8> bytes = element.bytes;
        if (bytes.size() != CameraSize || element.type == nullptr) {
            return std::nullopt;
        }
        const u16 setting = readHalf(bytes, 0);
        std::string settingName = std::format("{}", setting);
        if (const cparser::Enumeration* settings = types.findEnumeration("CameraSettingType")) {
            const auto found =
                std::ranges::find(settings->constants, s64{setting}, &cparser::Enumerator::value);
            if (found != settings->constants.end()) {
                settingName = found->name;
            }
        }
        std::string data = "NULL";
        const u32 word = readWord(bytes, CameraDataOffset);
        if (const z64::Relocation* relocation = element.findRelocation(CameraDataOffset)) {
            const auto field =
                std::ranges::find(element.type->fields, CameraDataOffset, &cparser::Field::offset);
            if (field == element.type->fields.end() || field->type == nullptr) {
                return std::nullopt;
            }
            data = writer.writeAddress(*relocation, word, *field->type);
        } else if (word != 0) {
            return std::nullopt;
        }
        return std::format("{{ {}, {}, {} }}", settingName, readSigned(bytes, 2), data);
    };
}

} // namespace toki::scene
