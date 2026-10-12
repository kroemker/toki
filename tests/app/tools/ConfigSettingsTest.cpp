#include "app/tools/configure/domain/ConfigSettings.h"

#include "base/Diagnostic.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path getFixture() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

} // namespace

TEST_CASE("each setting is edited by what it is written as", "[tools][configure]") {
    const std::vector<hackeroot::ConfigDefine> defines{
        {.name = "BOOT_TO_SCENE", .file = "include/config/config_debug.h", .written = "false"},
        {.name = "DEBUG_HEAP_SIZE", .file = "include/config/config_debug.h", .written = "0x600000"},
        {.name = "BLOCK_PUSH_SPEED", .file = "include/config/config_game.h", .written = "1.0f"},
        {.name = "BOOT_ENTRANCE",
         .file = "include/config/config_game.h",
         .written = "ENTR_HYRULE_FIELD_0"},
        {.name = "FA_CONTROL", .file = "include/config/config_game.h", .written = "BTN_R"},
        {.name = "WIDTH", .file = "include/config/config_game.h", .written = "(1 << 2)"},
    };

    const std::vector<toki::configure::ConfigGroup> groups = toki::configure::makeConfigGroups(defines);

    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].title == "Debug");
    REQUIRE(groups[1].title == "Game");
    REQUIRE(groups[0].settings[0].label == "Boot To Scene");
    REQUIRE(groups[0].settings[0].control == toki::configure::SettingControl::Toggle);
    REQUIRE(groups[0].settings[1].control == toki::configure::SettingControl::Integer);
    REQUIRE(groups[1].settings[0].control == toki::configure::SettingControl::Decimal);
    REQUIRE(groups[1].settings[1].collection == "EntranceIndex");
    REQUIRE(groups[1].settings[2].options.size() == 14);
    REQUIRE(groups[1].settings[3].control == toki::configure::SettingControl::Text);
    REQUIRE(toki::configure::isValidValue(groups[0].settings[1], "0x10"));
    REQUIRE_FALSE(toki::configure::isValidValue(groups[0].settings[1], "big"));
    REQUIRE_FALSE(toki::configure::isValidValue(groups[1].settings[2], "BTN_X"));
}

TEST_CASE("a change of the settings is a plan of their lines, warned of when overridden",
          "[tools][configure]") {
    const std::vector<toki::configure::ConfigGroup> groups =
        toki::configure::makeConfigGroups(hackeroot::readConfigDefines(getFixture(), {}));
    const std::map<std::string, std::string, std::less<>> values{{"INCLUDE_EXAMPLE_SCENE", "false"},
                                                                 {"DEBUG_BUFFER_SIZE", "0x20"}};

    const std::expected<hackeroot::EditPlan, std::string> plan =
        toki::configure::makeConfigPlan(getFixture(), groups, values);

    REQUIRE(plan.has_value());
    REQUIRE(plan->files.size() == 1);
    const hackeroot::FileEdit& edit = plan->files.front();
    REQUIRE(edit.file == std::filesystem::path("include/config/config_debug.h"));
    REQUIRE(edit.items.size() == 2);
    REQUIRE(edit.after.contains("#define INCLUDE_EXAMPLE_SCENE false\n"));
    REQUIRE(edit.after.contains("#define DEBUG_BUFFER_SIZE 0x100 // in bytes"));
    REQUIRE(edit.after.contains("    #define DEBUG_BUFFER_SIZE 0x20\n"));
    REQUIRE(plan->warnings.size() == 1);
    REQUIRE(plan->warnings.front().contains("INCLUDE_EXAMPLE_SCENE"));

    const std::map<std::string, std::string, std::less<>> bad{{"DEBUG_BUFFER_SIZE", "many"}};
    REQUIRE_FALSE(toki::configure::makeConfigPlan(getFixture(), groups, bad).has_value());
}

TEST_CASE("a real checkout's settings get the controls their values want",
          "[tools][configure][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());
    const std::vector<toki::configure::ConfigGroup> groups = toki::configure::makeConfigGroups(
        hackeroot::readConfigDefines(repository, workspace->getBuildDefines().arguments));

    std::map<std::string, std::string, std::less<>> values;
    std::map<std::string, toki::configure::ConfigSetting, std::less<>> settings;
    for (const toki::configure::ConfigGroup& group : groups) {
        for (const toki::configure::ConfigSetting& setting : group.settings) {
            values[setting.define.name] = setting.define.written;
            settings.emplace(setting.define.name, setting);
        }
    }
    REQUIRE(groups.size() >= 6);
    CHECK(settings.at("BOOT_TO_SCENE").control == toki::configure::SettingControl::Toggle);
    CHECK(settings.at("BOOT_ENTRANCE").collection == "EntranceIndex");
    CHECK(settings.at("MAP_SELECT_BGM").collection == "NA_BGM");
    CHECK(settings.at("CS_CTRL_STOP_CONTROL").options.size() == 14);
    CHECK(settings.at("BOOT_AGE").options.size() == 2);
    CHECK(settings.at("BLOCK_PUSH_SPEED").control == toki::configure::SettingControl::Decimal);

    const std::expected<hackeroot::EditPlan, std::string> unchanged =
        toki::configure::makeConfigPlan(repository, groups, values);
    REQUIRE(unchanged.has_value());
    CHECK_FALSE(unchanged->hasWork());
}
