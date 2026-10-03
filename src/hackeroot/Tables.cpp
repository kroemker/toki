#include "Tables.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/TableReader.h"
#include "hackeroot/Types.h"
#include "z64/DecompDialect.h"

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::array SceneMacros = {std::string_view("DEFINE_SCENE")};
constexpr std::array ActorMacros = {std::string_view("DEFINE_ACTOR"),
                                    std::string_view("DEFINE_ACTOR_INTERNAL"),
                                    std::string_view("DEFINE_ACTOR_UNSET")};
constexpr std::array ObjectMacros = {std::string_view("DEFINE_OBJECT"),
                                     std::string_view("DEFINE_OBJECT_UNSET")};
constexpr std::array EntranceMacros = {std::string_view("DEFINE_ENTRANCE")};

[[nodiscard]] std::string getArgument(const cparser::TableRow& row, usize index) {
    return index < row.arguments.size() ? std::string(row.arguments[index]) : std::string();
}

class Reader {
public:
    Reader(const cparser::MacroTable& macros, std::filesystem::path tables, Tables& result)
        : macros(macros), tables(std::move(tables)), result(result) {}

    [[nodiscard]] cparser::TableResult readTable(std::string_view file,
                                                 std::span<const std::string_view> entries);

    void requireArguments(const cparser::TableRow& row, usize expected);

private:
    const cparser::MacroTable& macros;
    std::filesystem::path tables;
    Tables& result;
};

cparser::TableResult Reader::readTable(std::string_view file,
                                       std::span<const std::string_view> entries) {
    std::expected<cparser::TableResult, base::Diagnostic> table =
        cparser::readTable(macros, tables / file, entries,
                           cparser::PreprocessOptions{.dialect = &z64::getDecompDialect()});
    if (!table) {
        result.diagnostics.push_back(std::move(table.error()));
        return {};
    }
    for (base::Diagnostic& diagnostic : table->diagnostics) {
        result.diagnostics.push_back(std::move(diagnostic));
    }
    table->diagnostics.clear();
    return std::move(*table);
}

void Reader::requireArguments(const cparser::TableRow& row, usize expected) {
    if (row.arguments.size() < expected) {
        result.diagnostics.push_back(base::makeWarning(
            row.location, std::format("{} has {} arguments where {} were expected", row.macro,
                                      row.arguments.size(), expected)));
    }
}

} // namespace

Tables readTables(const cparser::MacroTable& macros, const std::filesystem::path& repository) {
    Tables result;
    Reader reader(macros, repository / "include/tables", result);

    const cparser::TableResult scenes = reader.readTable("scene_table.h", SceneMacros);
    for (const cparser::TableRow& row : scenes.rows) {
        reader.requireArguments(row, 4);
        result.scenes.push_back(SceneEntry{.id = row.ordinal,
                                           .name = getArgument(row, 0),
                                           .titleCard = getArgument(row, 1),
                                           .enumName = getArgument(row, 2),
                                           .drawConfig = getArgument(row, 3)});
    }

    const cparser::TableResult actors = reader.readTable("actor_table.h", ActorMacros);
    for (const cparser::TableRow& row : actors.rows) {
        const bool isUnset = row.macro == "DEFINE_ACTOR_UNSET";
        reader.requireArguments(row, isUnset ? 1 : 2);
        result.actors.push_back(ActorEntry{.id = row.ordinal,
                                           .name = isUnset ? std::string() : getArgument(row, 0),
                                           .enumName = getArgument(row, isUnset ? 0 : 1),
                                           .isUnset = isUnset});
    }

    const cparser::TableResult objects = reader.readTable("object_table.h", ObjectMacros);
    for (const cparser::TableRow& row : objects.rows) {
        const bool isUnset = row.macro == "DEFINE_OBJECT_UNSET";
        reader.requireArguments(row, isUnset ? 1 : 2);
        result.objects.push_back(ObjectEntry{.id = row.ordinal,
                                             .name = isUnset ? std::string() : getArgument(row, 0),
                                             .enumName = getArgument(row, isUnset ? 0 : 1),
                                             .isUnset = isUnset});
    }

    const cparser::TableResult entrances = reader.readTable("entrance_table.h", EntranceMacros);
    for (const cparser::TableRow& row : entrances.rows) {
        reader.requireArguments(row, 3);
        result.entrances.push_back(EntranceEntry{.id = row.ordinal,
                                                 .enumName = getArgument(row, 0),
                                                 .sceneEnumName = getArgument(row, 1),
                                                 .spawn = getArgument(row, 2)});
    }
    return result;
}

} // namespace hackeroot
