#include "Settings.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

[[nodiscard]] std::expected<std::string, std::string> readFile(const std::filesystem::path& path) {
    const std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("cannot open {} for reading", toUtf8(path)));
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad()) {
        return std::unexpected(std::format("cannot read {}", toUtf8(path)));
    }
    return std::move(contents).str();
}

[[nodiscard]] std::expected<toml::table, std::string> parseFile(const std::filesystem::path& path) {
    const std::expected<std::string, std::string> contents = readFile(path);
    if (!contents) {
        return std::unexpected(contents.error());
    }

    try {
        return toml::parse(*contents, toUtf8(path));
    } catch (const toml::parse_error& error) {
        return std::unexpected(std::format("{}:{}:{}: {}", toUtf8(path), error.source().begin.line,
                                           error.source().begin.column, error.description()));
    }
}

[[nodiscard]] toml::table& section(toml::table& root, std::string_view name) {
    if (toml::table* existing = root[name].as_table(); existing != nullptr) {
        return *existing;
    }
    root.insert_or_assign(name, toml::table{});
    return *root[name].as_table();
}

void readLog(const toml::table& root, const std::filesystem::path& path, Settings& settings) {
    const toml::table* log = root["log"].as_table();
    if (log == nullptr) {
        return;
    }

    if (const std::optional<std::string> level = (*log)["min_level"].value<std::string>()) {
        if (const std::optional<LogLevel> parsed = logLevelFromName(*level)) {
            settings.log.minLevel = *parsed;
        } else {
            logWarn("{}: unknown log.min_level '{}', using '{}'", toUtf8(path), *level,
                    logLevelName(settings.log.minLevel));
        }
    }

    if (const std::optional<s64> maxEntries = (*log)["max_entries"].value<s64>()) {
        if (*maxEntries > 0) {
            settings.log.maxEntries = static_cast<usize>(*maxEntries);
        } else {
            logWarn("{}: log.max_entries must be positive, using {}", toUtf8(path),
                    settings.log.maxEntries);
        }
    }
}

void readWindow(const toml::table& root, Settings& settings) {
    const toml::table* window = root["window"].as_table();
    if (window == nullptr) {
        return;
    }

    const std::optional<s32> width = (*window)["width"].value<s32>();
    const std::optional<s32> height = (*window)["height"].value<s32>();
    const std::optional<s32> x = (*window)["x"].value<s32>();
    const std::optional<s32> y = (*window)["y"].value<s32>();
    if (!width || !height || !x || !y) {
        return;
    }

    settings.window = WindowGeometry{.x = *x,
                                     .y = *y,
                                     .width = *width,
                                     .height = *height,
                                     .maximized = (*window)["maximized"].value_or(false)};
}

void readPanels(const toml::table& root, Settings& settings) {
    const toml::array* open = root["panels"]["open"].as_array();
    if (open == nullptr) {
        return;
    }

    std::vector<std::string> ids;
    ids.reserve(open->size());
    for (const toml::node& node : *open) {
        if (const std::optional<std::string> id = node.value<std::string>()) {
            ids.push_back(*id);
        }
    }
    settings.openPanels = std::move(ids);
}

void writeLog(toml::table& root, const Settings& settings) {
    toml::table& log = section(root, "log");
    log.insert_or_assign("min_level", std::string{logLevelName(settings.log.minLevel)});
    log.insert_or_assign("max_entries", static_cast<s64>(settings.log.maxEntries));
}

void writeWindow(toml::table& root, const Settings& settings) {
    if (!settings.window) {
        root.erase("window");
        return;
    }

    toml::table& window = section(root, "window");
    window.insert_or_assign("x", settings.window->x);
    window.insert_or_assign("y", settings.window->y);
    window.insert_or_assign("width", settings.window->width);
    window.insert_or_assign("height", settings.window->height);
    window.insert_or_assign("maximized", settings.window->maximized);
}

void writePanels(toml::table& root, const Settings& settings) {
    if (!settings.openPanels) {
        root.erase("panels");
        return;
    }

    toml::array open;
    for (const std::string& id : *settings.openPanels) {
        open.push_back(id);
    }
    section(root, "panels").insert_or_assign("open", std::move(open));
}

} // namespace

std::expected<Settings, std::string> loadSettings(const std::filesystem::path& path) {
    std::error_code status;
    if (!std::filesystem::is_regular_file(path, status)) {
        return Settings{};
    }

    const std::expected<toml::table, std::string> root = parseFile(path);
    if (!root) {
        return std::unexpected(root.error());
    }

    Settings settings;
    if (const std::optional<std::string> checkout =
            (*root)["hackeroot_path"].value<std::string>()) {
        settings.hackerootPath = fromUtf8(*checkout);
    }

    readLog(*root, path, settings);
    readWindow(*root, settings);
    readPanels(*root, settings);

    return settings;
}

std::expected<void, std::string> saveSettings(const Settings& settings,
                                              const std::filesystem::path& path) {
    std::error_code status;
    if (const std::filesystem::path parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, status);
        if (status) {
            return std::unexpected(
                std::format("cannot create {}: {}", toUtf8(parent), status.message()));
        }
    }

    toml::table root;
    if (std::filesystem::is_regular_file(path, status)) {
        if (std::expected<toml::table, std::string> existing = parseFile(path)) {
            root = std::move(*existing);
        }
    }

    if (settings.hackerootPath) {
        root.insert_or_assign("hackeroot_path", toUtf8(*settings.hackerootPath));
    } else {
        root.erase("hackeroot_path");
    }
    writeLog(root, settings);
    writeWindow(root, settings);
    writePanels(root, settings);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return std::unexpected(std::format("cannot open {} for writing", toUtf8(path)));
    }

    stream << root << '\n';
    if (!stream.good()) {
        return std::unexpected(std::format("cannot write {}", toUtf8(path)));
    }
    return {};
}

} // namespace toki::core
