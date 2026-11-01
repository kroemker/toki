#include "MessageGeometry.h"

#include "app/actor/domain/OverlayCode.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string_view>
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
    return geometry;
}

} // namespace toki::message
