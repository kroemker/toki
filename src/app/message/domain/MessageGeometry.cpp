#include "MessageGeometry.h"

#include "app/actor/domain/OverlayCode.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <charconv>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace toki::message {

namespace {

constexpr std::string_view FontWidths = "sFontWidths";

[[nodiscard]] std::optional<std::vector<f64>> readNumbers(const actor::OverlayCode& code,
                                                          const actor::ArrayDefinition& array) {
    std::vector<f64> numbers;
    for (const std::span<const cparser::Token> element : array.elements) {
        const std::optional<f64> number = code.evaluateNumber(element);
        if (!number) {
            return std::nullopt;
        }
        numbers.push_back(*number);
    }
    return numbers;
}

class GeometryReader {
public:
    GeometryReader(const actor::OverlayCode& code, const std::filesystem::path& file,
                   MessageGeometry& geometry)
        : code(code), file(file), geometry(geometry), arrays(code.listArrays()) {}

    [[nodiscard]] std::optional<std::vector<f64>> readTable(std::string_view name,
                                                            usize minimumLength);

private:
    const actor::OverlayCode& code;
    const std::filesystem::path& file;
    MessageGeometry& geometry;
    std::vector<actor::ArrayDefinition> arrays;
};

std::optional<std::vector<f64>> GeometryReader::readTable(const std::string_view name,
                                                          const usize minimumLength) {
    const auto array = std::ranges::find(arrays, name, &actor::ArrayDefinition::name);
    if (array == arrays.end()) {
        geometry.diagnostics.push_back(base::makeWarning(
            {.file = file},
            std::format("src/code/z_message.c defines no {}, so text is laid out without it",
                        name)));
        return std::nullopt;
    }
    std::optional<std::vector<f64>> numbers = readNumbers(code, *array);
    if (!numbers || numbers->size() < minimumLength) {
        geometry.diagnostics.push_back(base::makeWarning(
            array->location,
            std::format("{} is not {} numbers Toki64 can read, so text is laid out without it",
                        name, minimumLength)));
        return std::nullopt;
    }
    return numbers;
}

void readBoxes(GeometryReader& reader, MessageGeometry& geometry) {
    struct Column {
        std::string_view name;
        s16 BoxPlacement::*field;
    };

    constexpr std::array Columns = {
        Column{"sTextboxXPositions", &BoxPlacement::x},
        Column{"sTextboxLowerYPositions", &BoxPlacement::lowerY},
        Column{"sTextboxUpperYPositions", &BoxPlacement::upperY},
        Column{"sTextboxMidYPositions", &BoxPlacement::middleY},
        Column{"sTextboxEndIconYOffset", &BoxPlacement::endIconOffset},
    };
    for (const Column& column : Columns) {
        const std::optional<std::vector<f64>> numbers = reader.readTable(column.name, BoxTypeCount);
        if (!numbers) {
            continue;
        }
        for (usize type = 0; type < BoxTypeCount; ++type) {
            geometry.boxes[type].*column.field = static_cast<s16>((*numbers)[type]);
        }
    }
}

} // namespace

MessageGeometry readMessageGeometry(const hackeroot::Workspace& workspace) {
    MessageGeometry geometry;
    const hackeroot::OverlayFiles message{.directory = workspace.getRoot() / "src" / "code",
                                          .main =
                                              workspace.getRoot() / "src" / "code" / "z_message.c"};
    geometry.files.push_back(message.main);
    const std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        hackeroot::preprocessOverlay(workspace, message);
    if (!unit) {
        geometry.diagnostics.push_back(unit.error());
        return geometry;
    }
    const actor::OverlayCode code(*unit, message.main);
    GeometryReader reader(code, message.main, geometry);
    if (const std::optional<std::vector<f64>> widths =
            reader.readTable(FontWidths, FontCharacterCount)) {
        for (usize index = 0; index < FontCharacterCount; ++index) {
            geometry.fontWidths[index] = static_cast<f32>((*widths)[index]);
        }
    }
    readBoxes(reader, geometry);

    const std::filesystem::path kanread = workspace.getRoot() / "src" / "code" / "kanread.s";
    geometry.files.push_back(kanread);
    std::ifstream stream(kanread, std::ios::binary);
    const std::string assembly((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    geometry.nonKanjiIndices = parseNonKanjiIndices(assembly);
    if (geometry.nonKanjiIndices.empty()) {
        geometry.diagnostics.push_back(base::makeWarning(
            {.file = kanread}, "sNonKanjiIndices could not be read, so Japanese is drawn without "
                               "its kana and signs"));
    }
    return geometry;
}

std::vector<u16> parseNonKanjiIndices(const std::string_view assembly) {
    std::vector<u16> indices;
    const usize begin = assembly.find("DATA(sNonKanjiIndices)");
    const usize end = assembly.find("ENDDATA(sNonKanjiIndices)");
    if (begin == std::string_view::npos || end == std::string_view::npos || end < begin) {
        return indices;
    }
    const std::string_view table = assembly.substr(begin, end - begin);
    usize at = 0;
    while ((at = table.find(".half", at)) != std::string_view::npos) {
        at += 5;
        const usize lineEnd = std::min(table.find('\n', at), table.size());
        std::string_view values = table.substr(at, lineEnd - at);
        while (!values.empty()) {
            const usize comma = std::min(values.find(','), values.size());
            std::string_view value = values.substr(0, comma);
            constexpr std::string_view Blank = " \t\r";
            while (!value.empty() && Blank.contains(value.front())) {
                value.remove_prefix(1);
            }
            while (!value.empty() && Blank.contains(value.back())) {
                value.remove_suffix(1);
            }
            if (value.starts_with("0x")) {
                value.remove_prefix(2);
            }
            u32 parsed = 0;
            const auto [next, error] =
                std::from_chars(value.data(), value.data() + value.size(), parsed, 16);
            if (error != std::errc() || next != value.data() + value.size()) {
                return {};
            }
            indices.push_back(static_cast<u16>(parsed));
            values.remove_prefix(std::min(comma + 1, values.size()));
        }
        at = lineEnd;
    }
    return indices;
}

std::optional<u32> findKanjiOffset(const u16 code, const MessageGeometry& geometry) {
    constexpr u32 GlyphsPerLead = 0xBC;
    constexpr u32 FirstKanji = 0x8800;
    constexpr u32 FirstKanjiIndex = 0x30A;
    constexpr u32 GlyphSize = 128;
    u32 lead = code >> 8U;
    u32 trail = (code & 0xFFU) - 0x40U;
    if (trail >= 0x40U) {
        --trail;
    }
    if (code >= FirstKanji) {
        lead -= 0x88U;
        return (FirstKanjiIndex + trail + lead * GlyphsPerLead) * GlyphSize;
    }
    lead -= 0x81U;
    const u32 index = trail + lead * GlyphsPerLead;
    if (index >= geometry.nonKanjiIndices.size()) {
        return std::nullopt;
    }
    return u32{geometry.nonKanjiIndices[index]} * GlyphSize;
}

} // namespace toki::message
