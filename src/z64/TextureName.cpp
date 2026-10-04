#include "TextureName.h"

#include "n64/TileDescriptor.h"
#include "z64/Types.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace z64 {

namespace {

struct FormatName {
    std::string_view name;
    n64::TextureFormat format;
    n64::TexelSize size;
};

constexpr std::array<FormatName, 9> Formats{{
    {"i4", n64::TextureFormat::Intensity, n64::TexelSize::Bits4},
    {"i8", n64::TextureFormat::Intensity, n64::TexelSize::Bits8},
    {"ci4", n64::TextureFormat::ColorIndex, n64::TexelSize::Bits4},
    {"ci8", n64::TextureFormat::ColorIndex, n64::TexelSize::Bits8},
    {"ia4", n64::TextureFormat::IntensityAlpha, n64::TexelSize::Bits4},
    {"ia8", n64::TextureFormat::IntensityAlpha, n64::TexelSize::Bits8},
    {"ia16", n64::TextureFormat::IntensityAlpha, n64::TexelSize::Bits16},
    {"rgba16", n64::TextureFormat::Rgba, n64::TexelSize::Bits16},
    {"rgba32", n64::TextureFormat::Rgba, n64::TexelSize::Bits32},
}};

constexpr std::string_view PalettePrefix = "tlut_";
constexpr usize MaxSuffixes = 5;

std::optional<usize> findElementSize(const std::string_view suffix) {
    if (suffix == "u64") {
        return 8;
    }
    if (suffix == "u32") {
        return 4;
    }
    return std::nullopt;
}

std::vector<std::string_view> splitSuffixes(std::string_view rest) {
    std::vector<std::string_view> suffixes;
    while (suffixes.size() < MaxSuffixes) {
        const usize dot = rest.rfind('.');
        if (dot == std::string_view::npos) {
            break;
        }
        suffixes.push_back(rest.substr(dot + 1));
        rest = rest.substr(0, dot);
    }
    return suffixes;
}

bool readSharedPalette(const std::string_view suffix, TextureName& name) {
    std::string_view palette = suffix.substr(PalettePrefix.size());
    if (palette.ends_with("_u32")) {
        name.paletteElementSize = 4;
        palette.remove_suffix(4);
    } else if (palette.ends_with("_u64")) {
        palette.remove_suffix(4);
    }
    if (palette.empty()) {
        return false;
    }
    name.sharedPalette = std::string(palette);
    return true;
}

bool isValidSplit(const TextureName& name) {
    return name.paletteHalf == PaletteHalf::Whole ||
           (name.format == n64::TextureFormat::ColorIndex && name.size == n64::TexelSize::Bits8 &&
            name.sharedPalette.has_value());
}

} // namespace

std::optional<TextureName> parseTextureName(const std::string_view fileName) {
    const std::vector<std::string_view> suffixes = splitSuffixes(fileName);
    if (suffixes.empty() || suffixes[0] != "png") {
        return std::nullopt;
    }

    TextureName name;
    usize at = 1;
    if (at < suffixes.size()) {
        if (const std::optional<usize> elementSize = findElementSize(suffixes[at])) {
            name.elementSize = *elementSize;
            ++at;
        }
    }
    if (at < suffixes.size() && suffixes[at].starts_with(PalettePrefix)) {
        if (!readSharedPalette(suffixes[at], name)) {
            return std::nullopt;
        }
        ++at;
    }
    if (at < suffixes.size() && (suffixes[at] == "split_lo" || suffixes[at] == "split_hi")) {
        name.paletteHalf = suffixes[at] == "split_lo" ? PaletteHalf::Low : PaletteHalf::High;
        ++at;
    }
    if (at >= suffixes.size()) {
        return std::nullopt;
    }
    const std::string_view format = suffixes[at];
    const auto found = std::ranges::find(Formats, format, &FormatName::name);
    if (found == Formats.end()) {
        return std::nullopt;
    }
    name.format = found->format;
    name.size = found->size;
    if (!isValidSplit(name)) {
        return std::nullopt;
    }
    if (name.format != n64::TextureFormat::ColorIndex) {
        name.sharedPalette.reset();
    }
    const usize prefixLength = static_cast<usize>(format.data() - fileName.data()) - 1;
    name.prefix = std::string(fileName.substr(0, prefixLength));
    return name;
}

} // namespace z64
