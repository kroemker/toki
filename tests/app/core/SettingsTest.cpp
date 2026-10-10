#include "app/core/domain/Settings.h"

#include "app/core/domain/Log.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using toki::core::loadSettings;
using toki::core::LogLevel;
using toki::core::saveSettings;
using toki::core::Settings;
using toki::core::WindowGeometry;

namespace {

class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "toki_tests" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    [[nodiscard]] std::filesystem::path getFile() const {
        return path / "config.toml";
    }

private:
    std::filesystem::path path;
};

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("a missing config yields defaults, not a failure", "[core][settings]") {
    const TempDir dir("missing");

    const auto loaded = loadSettings(dir.getFile());

    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->isConfigured());
}

TEST_CASE("an empty config yields defaults", "[core][settings]") {
    const TempDir dir("empty");
    writeFile(dir.getFile(), "");

    const auto loaded = loadSettings(dir.getFile());

    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->isConfigured());
    REQUIRE(loaded->log.minLevel == LogLevel::Debug);
}

TEST_CASE("settings round-trip through a file", "[core][settings]") {
    const TempDir dir("roundtrip");

    Settings written;
    written.hackerootPath = std::filesystem::path("D:/Projects/HackerOoT");
    written.log.minLevel = LogLevel::Warn;
    written.window =
        WindowGeometry{.x = 120, .y = 80, .width = 1280, .height = 720, .maximized = true};
    written.openPanels = std::vector<std::string>{"log", "assets"};

    REQUIRE(saveSettings(written, dir.getFile()).has_value());

    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded.has_value());

    const Settings& read = *loaded;
    REQUIRE(read.hackerootPath == written.hackerootPath);
    REQUIRE(read.log.minLevel == LogLevel::Warn);
    REQUIRE(read.window.has_value());
    REQUIRE(read.window->x == 120);
    REQUIRE(read.window->height == 720);
    REQUIRE(read.window->maximized);
    REQUIRE(read.openPanels == written.openPanels);
}

TEST_CASE("saving creates the config directory", "[core][settings]") {
    const TempDir dir("nested");
    const std::filesystem::path nested = dir.getFile().parent_path() / "a" / "b" / "config.toml";

    REQUIRE(saveSettings(Settings{}, nested).has_value());
    REQUIRE(std::filesystem::is_regular_file(nested));
}

TEST_CASE("a malformed config reports where it broke", "[core][settings]") {
    const TempDir dir("malformed");
    writeFile(dir.getFile(), "hackeroot_path = \n");

    const auto loaded = loadSettings(dir.getFile());

    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().contains("config.toml"));
}

TEST_CASE("an unknown log level falls back instead of failing", "[core][settings]") {
    const TempDir dir("bad_level");
    writeFile(dir.getFile(), "[log]\nmin_level = \"verbose\"\n");

    const auto loaded = loadSettings(dir.getFile());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->log.minLevel == LogLevel::Debug);
}

TEST_CASE("a partial window section is discarded", "[core][settings]") {
    const TempDir dir("partial_window");
    writeFile(dir.getFile(), "[window]\nwidth = 800\n");

    const auto loaded = loadSettings(dir.getFile());

    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->window.has_value());
}

TEST_CASE("keys this build does not know are preserved", "[core][settings]") {
    const TempDir dir("unknown_keys");
    writeFile(dir.getFile(), "future_option = 42\n\n[future]\nnested = \"kept\"\n");

    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded.has_value());
    REQUIRE(saveSettings(*loaded, dir.getFile()).has_value());

    const std::string contents = readFile(dir.getFile());
    REQUIRE(contents.contains("future_option"));
    REQUIRE(contents.contains("kept"));
}

TEST_CASE("the log cap round-trips and rejects nonsense", "[core][settings]") {
    const TempDir dir("log_cap");

    Settings written;
    written.log.maxEntries = 250;
    REQUIRE(saveSettings(written, dir.getFile()).has_value());

    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->log.maxEntries == 250);

    writeFile(dir.getFile(), "[log]\nmax_entries = 0\n");
    const auto rejected = loadSettings(dir.getFile());
    REQUIRE(rejected.has_value());
    REQUIRE(rejected->log.maxEntries == toki::core::Log::DefaultMaxEntries);
}

TEST_CASE("build settings round-trip and accept numbers", "[core][settings]") {
    const TempDir dir("build_settings");
    writeFile(dir.getFile(),
              "[build]\nVERSION = \"gc-eu-mq-dbg\"\nDEBUG_FEATURES = 0\nBAD = true\n");

    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->build.at("VERSION") == "gc-eu-mq-dbg");
    REQUIRE(loaded->build.at("DEBUG_FEATURES") == "0");
    REQUIRE_FALSE(loaded->build.contains("BAD"));

    REQUIRE(saveSettings(*loaded, dir.getFile()).has_value());
    const auto reloaded = loadSettings(dir.getFile());
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->build == loaded->build);
}

TEST_CASE("closed panels round-trip beside open ones", "[core][settings]") {
    const TempDir dir("closed_panels");
    Settings written;
    written.openPanels = std::vector<std::string>{"log"};
    written.closedPanels = {"workspace"};

    REQUIRE(saveSettings(written, dir.getFile()).has_value());
    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->openPanels == std::vector<std::string>{"log"});
    REQUIRE(loaded->closedPanels == std::vector<std::string>{"workspace"});
}

TEST_CASE("the scene last open round-trips with how it was shown", "[core][settings]") {
    const TempDir dir("session");
    Settings written;
    written.session = toki::core::SceneSession{
        .directory = std::filesystem::path("//wsl.localhost/Ubuntu/home/leo/HackerOoT/spot04"),
        .layer = 2,
        .lightSetting = 3,
        .hiddenRooms = {1, 4},
        .marksActors = false,
        .drawsModels = false,
        .camera = toki::core::CameraPose{
            .target = {1522.0F, -8.5F, 105.0F}, .yaw = 30.0F, .pitch = 12.5F, .distance = 400.0F}};

    REQUIRE(saveSettings(written, dir.getFile()).has_value());
    const auto loaded = loadSettings(dir.getFile());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->session.has_value());
    const toki::core::SceneSession& session = *loaded->session;
    REQUIRE(session.directory == written.session->directory);
    REQUIRE(session.layer == 2);
    REQUIRE(session.lightSetting == 3);
    REQUIRE(session.hiddenRooms == std::vector<base::usize>{1, 4});
    REQUIRE_FALSE(session.marksActors);
    REQUIRE_FALSE(session.drawsModels);
    REQUIRE(session.camera.has_value());
    REQUIRE(session.camera->target[1] == -8.5F);
    REQUIRE(session.camera->distance == 400.0F);
}

TEST_CASE("a session without its scene is dropped, a broken camera only loses the camera",
          "[core][settings]") {
    const TempDir dir("broken_session");
    writeFile(dir.getFile(), "[session]\nlayer = 1\n");
    REQUIRE_FALSE(loadSettings(dir.getFile())->session.has_value());

    writeFile(dir.getFile(), "[session]\nscene = \"x\"\ncamera = { yaw = 1.0 }\n");
    const auto loaded = loadSettings(dir.getFile());
    REQUIRE(loaded->session.has_value());
    REQUIRE_FALSE(loaded->session->camera.has_value());
}

TEST_CASE("a path is the same written with either separator", "[core][settings]") {
    REQUIRE(toki::core::isSamePath(R"(//wsl.localhost/Ubuntu/home/leo/HackerOoT/assets/scenes/x)",
                                   R"(//wsl.localhost/Ubuntu/home/leo/HackerOoT\assets\scenes\x)"));
    REQUIRE(toki::core::isSamePath(R"(D:/a/b/../c)", R"(D:\a\c)"));
    REQUIRE_FALSE(toki::core::isSamePath(R"(D:/a/b)", R"(D:/a/c)"));
}
