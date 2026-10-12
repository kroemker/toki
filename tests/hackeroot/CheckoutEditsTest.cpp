#include "hackeroot/CheckoutEdits.h"

#include "base/Diagnostic.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path makeFolder(const std::string& name) {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "toki_tests" / name;
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);
    return folder;
}

void writeRaw(const std::filesystem::path& file, const std::string& text) {
    std::ofstream stream(file, std::ios::binary);
    stream << text;
}

[[nodiscard]] std::string readRaw(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] std::filesystem::path getFixture() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

} // namespace

TEST_CASE("a define is set on its line, keeping its indent and comment", "[hackeroot][edits]") {
    std::string text =
        "#if X\n    #define SIZE 0x100 // in bytes\n#else\n#define SIZE 0x10\n#endif\n";

    REQUIRE(hackeroot::setDefine(text, 2, "SIZE", "0x100", "0x200") ==
            hackeroot::EditStatus::Update);
    REQUIRE(text ==
            "#if X\n    #define SIZE 0x200 // in bytes\n#else\n#define SIZE 0x10\n#endif\n");
    REQUIRE(hackeroot::setDefine(text, 2, "SIZE", "0x100", "0x200") ==
            hackeroot::EditStatus::Present);
    REQUIRE(hackeroot::setDefine(text, 3, "SIZE", "", "1") == hackeroot::EditStatus::Add);
}

TEST_CASE("a define is only set on a line that still reads as it was read", "[hackeroot][edits]") {
    const std::string written = "#define BOOT_TO_SCENE_NEW_GAME_ONLY false\n"
                                "#define BOOT_TO_SCENE false /* boots */\n"
                                "#define MULTI 1 /* starts\n ends */\n";
    std::string text = written;

    REQUIRE(hackeroot::setDefine(text, 1, "BOOT_TO_SCENE", "false", "true") ==
            hackeroot::EditStatus::Add);
    REQUIRE(hackeroot::setDefine(text, 2, "BOOT_TO_SCENE", "true", "false") ==
            hackeroot::EditStatus::Present);
    REQUIRE(hackeroot::setDefine(text, 2, "BOOT_TO_SCENE", "1", "true") ==
            hackeroot::EditStatus::Add);
    REQUIRE(hackeroot::setDefine(text, 3, "MULTI", "1", "2") == hackeroot::EditStatus::Add);
    REQUIRE(text == written);
    REQUIRE(hackeroot::setDefine(text, 2, "BOOT_TO_SCENE", "false", "true") ==
            hackeroot::EditStatus::Update);
    REQUIRE(text.contains("#define BOOT_TO_SCENE true /* boots */\n"));
}

TEST_CASE("Toki's entries go into its block, made once before the lines that stay last",
          "[hackeroot][edits]") {
    std::string text = "DEFINE_SCENE(a)\n\n#if TESTSUITE_MODE\n#include \"t.h\"\n#endif\n";

    hackeroot::addToManagedBlock(text, "custom scenes", "DEFINE_SCENE(b)", "#if TESTSUITE_MODE");
    hackeroot::addToManagedBlock(text, "custom scenes", "DEFINE_SCENE(c)", "#if TESTSUITE_MODE");

    REQUIRE(text == "DEFINE_SCENE(a)\n\n"
                    "// Toki64 custom scenes — managed block\n"
                    "DEFINE_SCENE(b)\n\nDEFINE_SCENE(c)\n\n"
                    "// Toki64 end\n\n"
                    "#if TESTSUITE_MODE\n#include \"t.h\"\n#endif\n");
    hackeroot::addToManagedBlock(text, "custom actors", "DEFINE_ACTOR(d)", "");
    REQUIRE(text.ends_with("// Toki64 custom actors — managed block\n"
                           "DEFINE_ACTOR(d)\n\n// Toki64 end\n\n"));
    REQUIRE(hackeroot::findManagedBlock(text, "custom scenes")->end <
            hackeroot::findManagedBlock(text, "custom actors")->begin);
}

TEST_CASE("a spec segment is added once, left alone, or rewritten where it is",
          "[hackeroot][edits]") {
    std::string spec = "beginseg\n    name \"room_10\"\nendseg\n\n#if TESTSUITE_MODE\n#endif\n";
    const std::string block = "beginseg\n    name \"room_1\"\n    number 3\nendseg";

    REQUIRE(hackeroot::putSpecSegment(spec, "room_1", block, "custom scenes",
                                      "#if TESTSUITE_MODE") == hackeroot::EditStatus::Add);
    REQUIRE(hackeroot::putSpecSegment(spec, "room_1", block, "custom scenes",
                                      "#if TESTSUITE_MODE") == hackeroot::EditStatus::Present);
    const std::string changed = "beginseg\n    name \"room_1\"\n    number 4\nendseg";
    REQUIRE(hackeroot::putSpecSegment(spec, "room_1", changed, "custom scenes",
                                      "#if TESTSUITE_MODE") == hackeroot::EditStatus::Update);
    REQUIRE(spec.contains("number 4"));
    REQUIRE_FALSE(spec.contains("number 3"));
    REQUIRE(spec.starts_with("beginseg\n    name \"room_10\"\nendseg"));
    REQUIRE(spec.ends_with("#if TESTSUITE_MODE\n#endif\n"));
}

TEST_CASE("the lines a rewritten segment loses are what was added to it by hand",
          "[hackeroot][edits]") {
    const std::string spec = "beginseg\n    name \"room_1\"\n    compress\n"
                             "    include \"$(BUILD_DIR)/mine.o\"\n    number 3\nendseg\n";
    const std::string block = "beginseg\n    name \"room_1\"\n    compress\n    number 3\nendseg";

    REQUIRE(hackeroot::listLostSegmentLines(spec, "room_1", block) ==
            std::vector<std::string>{"include \"$(BUILD_DIR)/mine.o\""});
    REQUIRE(hackeroot::listLostSegmentLines(spec, "room_2", block).empty());
}

TEST_CASE("an unset slot of a table is taken, unless it is one to keep", "[hackeroot][edits]") {
    std::string table = "/* 0x00 */ DEFINE_OBJECT_UNSET(OBJECT_INVALID)\n"
                        "/* 0x01 */ DEFINE_OBJECT(object_a, OBJECT_A)\n"
                        "/* 0x02 */ DEFINE_OBJECT_UNSET(OBJECT_UNSET_2)\n";

    REQUIRE(hackeroot::replaceUnsetEntry(table, "DEFINE_OBJECT_UNSET(",
                                         "DEFINE_OBJECT(object_b, OBJECT_B)", {"OBJECT_INVALID"}));
    REQUIRE(table.contains("/* 0x02 */ DEFINE_OBJECT(object_b, OBJECT_B)\n"));
    REQUIRE(table.contains("DEFINE_OBJECT_UNSET(OBJECT_INVALID)"));
    REQUIRE_FALSE(hackeroot::replaceUnsetEntry(table, "DEFINE_OBJECT_UNSET(", "DEFINE_OBJECT(c, C)",
                                               {"OBJECT_INVALID"}));

    std::string list = "static Scene s[] = {\n    { \"Field\", Load, F },\n};\n";
    hackeroot::insertLineBefore(list, "{ \"Field\", Load, F },", "    { \"Mine\", Load, M },");
    REQUIRE(list ==
            "static Scene s[] = {\n    { \"Mine\", Load, M },\n    { \"Field\", Load, F },\n};\n");
}

TEST_CASE("a plan is written with each file's line endings, and not when a file moved on",
          "[hackeroot][edits]") {
    const std::filesystem::path folder = makeFolder("checkout_edits");
    writeRaw(folder / "table.h", "a\r\nb\r\n");

    const std::expected<std::string, std::string> read =
        hackeroot::readCheckoutText(folder / "table.h");
    REQUIRE(read == "a\nb\n");
    hackeroot::EditPlan plan;
    plan.files.push_back(
        hackeroot::FileEdit{.file = "table.h", .before = *read, .after = "a\nc\n"});
    plan.files.push_back(hackeroot::FileEdit{.file = "src/new.c", .after = "int x;\n"});
    REQUIRE(plan.hasWork());

    const std::expected<hackeroot::EditsApplied, std::string> applied =
        hackeroot::applyEditPlan(folder, plan);
    REQUIRE(applied.has_value());
    REQUIRE(applied->written.size() == 2);
    REQUIRE(readRaw(folder / "table.h") == "a\r\nc\r\n");
    REQUIRE(readRaw(folder / "src/new.c") == "int x;\n");

    const std::expected<hackeroot::EditsApplied, std::string> stale =
        hackeroot::applyEditPlan(folder, plan);
    REQUIRE_FALSE(stale.has_value());
    REQUIRE(readRaw(folder / "table.h") == "a\r\nc\r\n");
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("a plan that fails to write a file puts back every file written before it",
          "[hackeroot][edits]") {
    const std::filesystem::path folder = makeFolder("checkout_edits_undo");
    writeRaw(folder / "table.h", "a\r\n");
    writeRaw(folder / "blocker", "a file where a folder is wanted");
    hackeroot::EditPlan plan;
    plan.files.push_back(hackeroot::FileEdit{.file = "table.h", .before = "a\n", .after = "b\n"});
    plan.files.push_back(hackeroot::FileEdit{.file = "made/one.c", .after = "int x;\n"});
    plan.files.push_back(hackeroot::FileEdit{.file = "blocker/two.c", .after = "int y;\n"});

    const std::expected<hackeroot::EditsApplied, std::string> applied =
        hackeroot::applyEditPlan(folder, plan);

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().contains("put back"));
    REQUIRE(readRaw(folder / "table.h") == "a\r\n");
    REQUIRE_FALSE(std::filesystem::exists(folder / "made/one.c"));
    REQUIRE_FALSE(std::filesystem::exists(folder / "made"));
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("the configuration's settings are read where the build defines them",
          "[hackeroot][config]") {
    const std::vector<std::string> debug{"-DDEBUG_FEATURES=1"};
    const std::vector<hackeroot::ConfigDefine> defines =
        hackeroot::readConfigDefines(getFixture(), debug);

    const auto find = [&defines](const std::string& name) {
        const auto found = std::ranges::find(defines, name, &hackeroot::ConfigDefine::name);
        REQUIRE(found != defines.end());
        return *found;
    };
    const hackeroot::ConfigDefine example = find("INCLUDE_EXAMPLE_SCENE");
    REQUIRE(example.file == std::filesystem::path("include/config/config_debug.h"));
    REQUIRE(example.line == 8);
    REQUIRE(example.written == "true");
    REQUIRE(example.comment == "Include example scene in the build");
    REQUIRE(example.effective == "true");
    REQUIRE(find("INCLUDE_TEST_SCENES").comment == "Include the test scenes");
    const hackeroot::ConfigDefine buffer = find("DEBUG_BUFFER_SIZE");
    REQUIRE(buffer.line == 13);
    REQUIRE(buffer.written == "0x100");
    REQUIRE_FALSE(
        std::ranges::contains(defines, "ONLY_WITHOUT_DEBUG", &hackeroot::ConfigDefine::name));
    REQUIRE_FALSE(std::ranges::contains(defines, "CONFIG_DEBUG_H", &hackeroot::ConfigDefine::name));

    const std::vector<hackeroot::ConfigDefine> release =
        hackeroot::readConfigDefines(getFixture(), {});
    const auto releaseBuffer =
        std::ranges::find(release, "DEBUG_BUFFER_SIZE", &hackeroot::ConfigDefine::name);
    REQUIRE(releaseBuffer->line == 15);
    const auto releaseExample =
        std::ranges::find(release, "INCLUDE_EXAMPLE_SCENE", &hackeroot::ConfigDefine::name);
    REQUIRE(releaseExample->written == "true");
    REQUIRE(releaseExample->effective == "false");
}

TEST_CASE("a real checkout's settings are each found on their own define line",
          "[hackeroot][config][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const std::vector<hackeroot::ConfigDefine> defines =
        hackeroot::readConfigDefines(repository, workspace->getBuildDefines().arguments);

    REQUIRE(defines.size() > 100);
    for (const hackeroot::ConfigDefine& define : defines) {
        const std::expected<std::string, std::string> text =
            hackeroot::readCheckoutText(repository / define.file);
        REQUIRE(text.has_value());
        std::string copy = *text;
        INFO(define.file.generic_string() << ":" << define.line << " " << define.name);
        CHECK(hackeroot::setDefine(copy, define.line, define.name, define.written,
                                   define.written) == hackeroot::EditStatus::Present);
    }
    for (const char* name : {"BOOT_TO_SCENE", "BOOT_ENTRANCE", "ENABLE_HACKER_DEBUG",
                             "BLOCK_PUSH_SPEED", "DEBUG_BUFFER_SIZE"}) {
        INFO(name);
        CHECK(std::ranges::contains(defines, std::string(name), &hackeroot::ConfigDefine::name));
    }
    const auto boot =
        std::ranges::find(defines, std::string("BOOT_TO_SCENE"), &hackeroot::ConfigDefine::name);
    CHECK_FALSE(boot->comment.empty());
}
