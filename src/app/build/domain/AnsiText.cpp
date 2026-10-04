#include "AnsiText.h"

#include "app/build/Types.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::build {

namespace {

constexpr char Escape = '\x1B';

constexpr std::array<Rgb, 8> Normal{{
    {0x00, 0x00, 0x00},
    {0xCD, 0x31, 0x31},
    {0x0D, 0xBC, 0x79},
    {0xE5, 0xE5, 0x10},
    {0x24, 0x72, 0xC8},
    {0xBC, 0x3F, 0xBC},
    {0x11, 0xA8, 0xCD},
    {0xE5, 0xE5, 0xE5},
}};

constexpr std::array<Rgb, 8> Bright{{
    {0x66, 0x66, 0x66},
    {0xF1, 0x4C, 0x4C},
    {0x23, 0xD1, 0x8B},
    {0xF5, 0xF5, 0x43},
    {0x3B, 0x8E, 0xEA},
    {0xD6, 0x70, 0xD6},
    {0x29, 0xB8, 0xDB},
    {0xFF, 0xFF, 0xFF},
}};

Rgb findStandardColor(const s32 index, const bool bright) {
    const usize slot = static_cast<usize>(index) % Normal.size();
    return bright ? Bright[slot] : Normal[slot];
}

Rgb findPaletteColor(const s32 index) {
    if (index < 16) {
        return findStandardColor(index % 8, index >= 8);
    }
    if (index < 232) {
        constexpr std::array<u8, 6> Levels{0, 95, 135, 175, 215, 255};
        const s32 cube = index - 16;
        return {Levels[static_cast<usize>(cube / 36)], Levels[static_cast<usize>((cube / 6) % 6)],
                Levels[static_cast<usize>(cube % 6)]};
    }
    const auto gray = static_cast<u8>(8 + ((index - 232) * 10));
    return {gray, gray, gray};
}

struct ExtendedColor {
    Rgb color;
    usize consumed = 0;
};

std::optional<ExtendedColor> findExtendedColor(const std::span<const s32> rest) {
    if (rest.size() >= 2 && rest[0] == 5) {
        return ExtendedColor{.color = findPaletteColor(rest[1] & 0xFF), .consumed = 2};
    }
    if (rest.size() >= 4 && rest[0] == 2) {
        return ExtendedColor{
            .color = {static_cast<u8>(rest[1]), static_cast<u8>(rest[2]), static_cast<u8>(rest[3])},
            .consumed = 4};
    }
    return std::nullopt;
}

std::vector<s32> parseCodes(const std::string_view parameters) {
    std::vector<s32> codes;
    usize start = 0;
    while (start <= parameters.size()) {
        const usize end = std::min(parameters.find(';', start), parameters.size());
        s32 code = 0;
        std::from_chars(parameters.data() + start, parameters.data() + end, code);
        codes.push_back(code);
        start = end + 1;
    }
    return codes;
}

bool applyAttribute(const s32 code, AnsiStyle& style) {
    switch (code) {
    case 0:
        style = AnsiStyle{};
        return true;
    case 1:
        style.bold = true;
        return true;
    case 3:
        style.italic = true;
        return true;
    case 4:
        style.underline = true;
        return true;
    case 22:
        style.bold = false;
        return true;
    case 23:
        style.italic = false;
        return true;
    case 24:
        style.underline = false;
        return true;
    case 39:
        style.foreground.reset();
        return true;
    case 49:
        style.background.reset();
        return true;
    default:
        return false;
    }
}

void applyCodes(const std::string_view parameters, AnsiStyle& style) {
    const std::vector<s32> codes = parseCodes(parameters);
    for (usize i = 0; i < codes.size(); ++i) {
        const s32 code = codes[i];
        if (applyAttribute(code, style)) {
            continue;
        }
        if (code >= 30 && code <= 37) {
            style.foreground = findStandardColor(code - 30, false);
        } else if (code >= 90 && code <= 97) {
            style.foreground = findStandardColor(code - 90, true);
        } else if (code >= 40 && code <= 47) {
            style.background = findStandardColor(code - 40, false);
        } else if (code >= 100 && code <= 107) {
            style.background = findStandardColor(code - 100, true);
        } else if (code == 38 || code == 48) {
            const std::optional<ExtendedColor> extended =
                findExtendedColor(std::span<const s32>(codes).subspan(i + 1));
            if (extended) {
                (code == 38 ? style.foreground : style.background) = extended->color;
                i += extended->consumed;
            }
        }
    }
}

bool isFinalByte(const char c) {
    return c >= '@' && c <= '~';
}

} // namespace

std::vector<AnsiSpan> parseAnsiLine(const std::string_view line) {
    std::vector<AnsiSpan> spans;
    AnsiStyle style;
    std::string text;
    const auto flush = [&] {
        if (!text.empty()) {
            spans.push_back(AnsiSpan{.text = std::move(text), .style = style});
            text.clear();
        }
    };
    usize position = 0;
    while (position < line.size()) {
        const char c = line[position];
        if (c == Escape && position + 1 < line.size() && line[position + 1] == '[') {
            usize end = position + 2;
            while (end < line.size() && !isFinalByte(line[end])) {
                ++end;
            }
            if (end < line.size() && line[end] == 'm') {
                flush();
                applyCodes(line.substr(position + 2, end - position - 2), style);
            }
            position = std::min(end + 1, line.size());
            continue;
        }
        if (static_cast<u8>(c) < 0x20 && c != '\t') {
            ++position;
            continue;
        }
        text += c;
        ++position;
    }
    flush();
    return spans;
}

} // namespace toki::build
