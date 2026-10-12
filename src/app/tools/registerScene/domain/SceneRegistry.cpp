#include "SceneRegistry.h"

#include "app/scene/domain/Scene.h"
#include "app/tools/Types.h"
#include "cparser/MacroTable.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/SceneFiles.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::tools {

namespace {

constexpr std::string_view SpecFile = "spec/spec";
constexpr std::string_view SceneTableFile = "include/tables/scene_table.h";
constexpr std::string_view EntranceTableFile = "include/tables/entrance_table.h";
constexpr std::string_view MapSelectFile = "src/overlays/gamestates/ovl_select/z_select.h";
constexpr std::string_view BlockTitle = "custom scenes";
constexpr std::string_view LastLines = "#if TESTSUITE_MODE";
constexpr std::string_view MapSelectAnchor =
    "{ \"Hyrule Field\", MapSelect_LoadGame, ENTR_HYRULE_FIELD_0 },";
constexpr usize EntranceLayers = 4;
constexpr usize SceneLimit = 255;

[[nodiscard]] std::string makeUpper(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](const char letter) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    });
    return upper;
}

[[nodiscard]] bool isIdentifier(const std::string_view name) {
    return !name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) == 0 &&
           std::ranges::all_of(name, [](const char letter) {
               return std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_';
           });
}

[[nodiscard]] std::string makeSegment(const std::string_view name,
                                      const std::vector<std::filesystem::path>& files,
                                      const int number) {
    std::string block =
        std::format("beginseg\n    name \"{}\"\n    compress\n    romalign 0x1000\n", name);
    for (const std::filesystem::path& file : files) {
        std::filesystem::path object = file;
        object.replace_extension(".o");
        block += std::format("    include \"$(BUILD_DIR)/{}\"\n", object.generic_string());
    }
    block += std::format("    number {}\nendseg", number);
    return block;
}

[[nodiscard]] std::string makeEntranceGroup(const std::string_view name, const usize spawn) {
    const std::string first = makeEntranceEnum(name, spawn);
    std::string block;
    for (usize layer = 0; layer < EntranceLayers; ++layer) {
        const std::string entrance = layer == 0 ? first : std::format("{}_{}", first, layer);
        block += std::format("{}DEFINE_ENTRANCE({}, {}, {}, false, true, TRANS_TYPE_FADE_BLACK, "
                             "TRANS_TYPE_FADE_BLACK)",
                             layer == 0 ? "" : "\n", entrance, makeSceneEnum(name), spawn);
    }
    return block;
}

[[nodiscard]] int rankSceneFile(const std::string_view suffix) {
    if (suffix == ".c" || suffix == "_main.c") {
        return 0;
    }
    if (suffix == "_col.c") {
        return 1;
    }
    return suffix.starts_with("_cs") ? 2 : 3;
}

[[nodiscard]] int rankRoomFile(const std::string_view suffix) {
    if (suffix == ".c" || suffix == "_main.c") {
        return 0;
    }
    if (suffix == "_model_info.c") {
        return 1;
    }
    return suffix == "_model.c" ? 2 : 3;
}

/// Orders the files of a segment as fast64 and the vanilla spec list them: the
/// one with the header first, then the scene's collision and cutscenes, or the
/// room's model info and model, then the rest by name.
void orderSegmentFiles(std::vector<std::filesystem::path>& files, const std::string& prefix,
                       const bool isRoom) {
    const auto rank = [&](const std::filesystem::path& file) {
        const std::string name = file.filename().string();
        std::string_view suffix(name);
        suffix.remove_prefix(std::min(prefix.size(), suffix.size()));
        if (isRoom) {
            suffix.remove_prefix(std::min(suffix.find_first_not_of("0123456789"), suffix.size()));
        }
        return std::pair(isRoom ? rankRoomFile(suffix) : rankSceneFile(suffix), name);
    };
    std::ranges::sort(files, {}, rank);
}

class PlanBuilder {
public:
    PlanBuilder(const std::filesystem::path& repository, const SceneExport& scene,
                const SceneRegistration& registration, const SceneSurvey& survey)
        : repository(repository), scene(scene), registration(registration), survey(survey) {}

    [[nodiscard]] std::expected<hackeroot::EditPlan, std::string> build() && {
        if (scene.rooms.empty() || scene.sceneFiles.empty()) {
            return std::unexpected(std::format("{} has no scene file or no room", scene.name));
        }
        spawns = registration.spawnCount.value_or(survey.spawns.value_or(1));
        if (spawns == 0) {
            return std::unexpected(std::string("a scene needs at least one spawn"));
        }
        if (registration.bootsToScene && registration.bootSpawn >= spawns) {
            return std::unexpected(std::format("spawn {} is not one of the {} registered",
                                               registration.bootSpawn, spawns));
        }
        if (registration.addsMapSelect && registration.mapSelectLabel.empty()) {
            return std::unexpected(std::string("the map select needs a name for the scene"));
        }
        warn();
        for (const auto step :
             {&PlanBuilder::planSpec, &PlanBuilder::planSceneTable, &PlanBuilder::planEntrances,
              &PlanBuilder::planMapSelect, &PlanBuilder::planBoot}) {
            if (std::expected<void, std::string> planned = (this->*step)(); !planned) {
                return std::unexpected(planned.error());
            }
        }
        return std::move(plan);
    }

private:
    [[nodiscard]] std::expected<hackeroot::FileEdit, std::string>
    openFile(const std::filesystem::path& file) const {
        std::expected<std::string, std::string> text =
            hackeroot::readCheckoutText(repository / file);
        if (!text) {
            return std::unexpected(text.error());
        }
        return hackeroot::FileEdit{.file = file, .before = *text, .after = *text};
    }

    void warn() {
        if (!isIdentifier(scene.name)) {
            plan.warnings.push_back(
                std::format("'{}' is not a C identifier, so the enums made of it will not compile",
                            scene.name));
        }
        if (survey.listedRooms && *survey.listedRooms != scene.rooms.size()) {
            plan.warnings.push_back(std::format(
                "the scene's room list names {} rooms, but its folder has {}; export it again",
                *survey.listedRooms, scene.rooms.size()));
        }
        if (!registration.spawnCount && !survey.spawns) {
            plan.warnings.push_back(
                std::string("the scene's spawns could not be read, so one is registered"));
        }
        const std::string segment = scene.name + "_scene";
        const bool isListed =
            std::ranges::contains(survey.scenes, segment, &hackeroot::SceneEntry::name);
        if (!isListed && survey.scenes.size() >= SceneLimit) {
            plan.warnings.push_back(std::format(
                "the scene table has {} scenes, and an entrance holds a scene's ID in 8 bits",
                survey.scenes.size()));
        }
        const auto taken =
            std::ranges::find_if(survey.scenes, [&](const hackeroot::SceneEntry& entry) {
                return entry.enumName == makeSceneEnum(scene.name) && entry.name != segment;
            });
        if (taken != survey.scenes.end()) {
            plan.warnings.push_back(
                std::format("{} is taken by {} already", taken->enumName, taken->name));
        }
        if (registration.bootsToScene && !survey.hasDebugFeatures) {
            plan.warnings.push_back(
                std::string("the build has no DEBUG_FEATURES, so config_safeguards.h turns "
                            "BOOT_TO_SCENE off and the game boots to its title screen"));
        }
    }

    [[nodiscard]] std::expected<void, std::string> planSpec() {
        std::expected<hackeroot::FileEdit, std::string> spec = openFile(SpecFile);
        if (!spec) {
            return std::unexpected(spec.error());
        }
        const std::string sceneSegment = scene.name + "_scene";
        const hackeroot::EditStatus status = hackeroot::putSpecSegment(
            spec->after, sceneSegment, makeSegment(sceneSegment, scene.sceneFiles, 2), BlockTitle,
            LastLines);
        spec->items.push_back(hackeroot::EditItem{
            .status = status, .label = std::format("scene segment \"{}\"", sceneSegment)});
        for (usize room = 0; room < scene.rooms.size(); ++room) {
            const std::string roomSegment = std::format("{}_room_{}", scene.name, room);
            spec->items.push_back(hackeroot::EditItem{
                .status = hackeroot::putSpecSegment(spec->after, roomSegment,
                                                    makeSegment(roomSegment, scene.rooms[room], 3),
                                                    BlockTitle, LastLines),
                .label = std::format("room segment \"{}\"", roomSegment)});
        }
        plan.files.push_back(std::move(*spec));
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> planSceneTable() {
        std::expected<hackeroot::FileEdit, std::string> table = openFile(SceneTableFile);
        if (!table) {
            return std::unexpected(table.error());
        }
        const std::string segment = scene.name + "_scene";
        const bool isPresent = table->after.contains(std::format("DEFINE_SCENE({},", segment));
        if (!isPresent) {
            hackeroot::addToManagedBlock(table->after, BlockTitle,
                                         std::format("DEFINE_SCENE({}, none, {}, {}, 0, 0)",
                                                     segment, makeSceneEnum(scene.name),
                                                     registration.drawConfig),
                                         LastLines);
        }
        table->items.push_back(hackeroot::EditItem{
            .status = isPresent ? hackeroot::EditStatus::Present : hackeroot::EditStatus::Add,
            .label = std::format("{} ({})", makeSceneEnum(scene.name), registration.drawConfig)});
        plan.files.push_back(std::move(*table));
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> planEntrances() {
        std::expected<hackeroot::FileEdit, std::string> table = openFile(EntranceTableFile);
        if (!table) {
            return std::unexpected(table.error());
        }
        for (usize spawn = 0; spawn < spawns; ++spawn) {
            const std::string entrance = makeEntranceEnum(scene.name, spawn);
            const bool isPresent =
                table->after.contains(std::format("DEFINE_ENTRANCE({},", entrance));
            if (!isPresent) {
                hackeroot::addToManagedBlock(table->after, BlockTitle,
                                             makeEntranceGroup(scene.name, spawn), LastLines);
            }
            table->items.push_back(hackeroot::EditItem{
                .status = isPresent ? hackeroot::EditStatus::Present : hackeroot::EditStatus::Add,
                .label = std::format("{}, for each of the four layers", entrance)});
        }
        plan.files.push_back(std::move(*table));
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> planMapSelect() {
        if (!registration.addsMapSelect) {
            return {};
        }
        std::expected<hackeroot::FileEdit, std::string> header = openFile(MapSelectFile);
        if (!header) {
            return std::unexpected(header.error());
        }
        const std::string entrance = makeEntranceEnum(scene.name, 0);
        const bool isPresent =
            std::ranges::any_of(std::views::split(header->after, '\n'), [&](const auto& line) {
                const std::string_view text(line.begin(), line.end());
                return text.contains("MapSelect_LoadGame") && text.contains(entrance + " ");
            });
        if (!isPresent && !header->after.contains(MapSelectAnchor)) {
            plan.warnings.push_back(std::format(
                "{} has no Hyrule Field entry to list the scene ahead of; list it yourself",
                MapSelectFile));
            return {};
        }
        if (!isPresent) {
            hackeroot::insertLineBefore(header->after, MapSelectAnchor,
                                        std::format("    {{ \"{}\", MapSelect_LoadGame, {} }},",
                                                    registration.mapSelectLabel, entrance));
        }
        header->items.push_back(hackeroot::EditItem{
            .status = isPresent ? hackeroot::EditStatus::Present : hackeroot::EditStatus::Add,
            .label = std::format("map select entry \"{}\"", registration.mapSelectLabel)});
        plan.files.push_back(std::move(*header));
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> planBoot() {
        if (!registration.bootsToScene) {
            return {};
        }
        std::vector<std::pair<std::string_view, std::string>> settings{
            {"BOOT_ENTRANCE", makeEntranceEnum(scene.name, registration.bootSpawn)},
            {"BOOT_TO_SCENE", "true"},
            {"ENABLE_HACKER_DEBUG", "true"},
        };
        if (registration.bootAge) {
            settings.emplace_back("BOOT_AGE", *registration.bootAge);
        }
        if (registration.bootTime) {
            settings.emplace_back("BOOT_TIME", *registration.bootTime);
        }
        std::vector<hackeroot::FileEdit> edits;
        for (const auto& [name, value] : settings) {
            const auto define =
                std::ranges::find(survey.config, name, &hackeroot::ConfigDefine::name);
            if (define == survey.config.end()) {
                plan.warnings.push_back(
                    std::format("the configuration has no {} to set to {}", name, value));
                continue;
            }
            auto edit = std::ranges::find(edits, define->file, &hackeroot::FileEdit::file);
            if (edit == edits.end()) {
                std::expected<hackeroot::FileEdit, std::string> opened = openFile(define->file);
                if (!opened) {
                    return std::unexpected(opened.error());
                }
                edits.push_back(std::move(*opened));
                edit = edits.end() - 1;
            }
            const hackeroot::EditStatus status =
                hackeroot::setDefine(edit->after, define->line, name, value);
            if (status == hackeroot::EditStatus::Add) {
                return std::unexpected(std::format("{}:{} no longer defines {}",
                                                   define->file.generic_string(), define->line,
                                                   name));
            }
            edit->items.push_back(hackeroot::EditItem{
                .status = status,
                .label = status == hackeroot::EditStatus::Present
                             ? std::format("{} {}", name, value)
                             : std::format("{}: {} → {}", name, define->written, value)});
        }
        std::ranges::move(edits, std::back_inserter(plan.files));
        return {};
    }

    const std::filesystem::path& repository;
    const SceneExport& scene;
    const SceneRegistration& registration;
    const SceneSurvey& survey;
    usize spawns = 1;
    hackeroot::EditPlan plan;
};

} // namespace

std::expected<SurveyedScene, std::string>
surveyScene(const std::shared_ptr<const hackeroot::Workspace>& workspace,
            const hackeroot::AssetDirectory& asset) {
    const hackeroot::SceneFiles files = hackeroot::findSceneFiles(asset);
    if (files.scene.empty() || files.rooms.empty()) {
        return std::unexpected(
            std::format("{} has no scene file and rooms that fast64 names", asset.name));
    }
    const std::filesystem::path& root = workspace->getRoot();
    const auto makeRelative = [&root](const std::filesystem::path& file) {
        return file.lexically_normal().lexically_relative(root.lexically_normal());
    };
    SurveyedScene surveyed;
    surveyed.scene.name = asset.name;
    surveyed.scene.directory = makeRelative(asset.directory);
    std::ranges::transform(files.scene, std::back_inserter(surveyed.scene.sceneFiles),
                           makeRelative);
    orderSegmentFiles(surveyed.scene.sceneFiles, asset.name + "_scene", false);
    for (const std::vector<std::filesystem::path>& room : files.rooms) {
        std::vector<std::filesystem::path>& relative = surveyed.scene.rooms.emplace_back();
        std::ranges::transform(room, std::back_inserter(relative), makeRelative);
        orderSegmentFiles(relative, asset.name + "_room_", true);
    }
    hackeroot::AssetImageReader reader(workspace);
    const scene::Scene read = scene::readScene(reader, asset);
    if (!read.layers.empty()) {
        surveyed.survey.spawns = read.layers.front().header.playerEntries.size();
        surveyed.survey.listedRooms = read.layers.front().rooms.size();
    }
    surveyed.survey.scenes = workspace->getTables().scenes;
    surveyed.survey.config =
        hackeroot::readConfigDefines(root, workspace->getBuildDefines().arguments);
    const cparser::Macro* debug = workspace->getMacros().find("DEBUG_FEATURES");
    surveyed.survey.hasDebugFeatures = debug != nullptr && !debug->body.empty() &&
                                       debug->body.front().text != "0" &&
                                       debug->body.front().text != "false";
    return surveyed;
}

std::string makeMapSelectLabel(const std::string_view name) {
    std::string label;
    bool startsWord = true;
    for (const char letter : name) {
        if (letter == '_' || letter == '-') {
            startsWord = true;
            continue;
        }
        if (startsWord && !label.empty()) {
            label += ' ';
        }
        label += startsWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(letter)))
                            : letter;
        startsWord = false;
    }
    return label;
}

std::string makeSceneEnum(const std::string_view name) {
    return "SCENE_" + makeUpper(name);
}

std::string makeEntranceEnum(const std::string_view name, const usize spawn) {
    return std::format("ENTR_{}_{}", makeUpper(name), spawn);
}

std::expected<hackeroot::EditPlan, std::string>
makeScenePlan(const std::filesystem::path& repository, const SceneExport& scene,
              const SceneRegistration& registration, const SceneSurvey& survey) {
    return PlanBuilder(repository, scene, registration, survey).build();
}

} // namespace toki::tools
