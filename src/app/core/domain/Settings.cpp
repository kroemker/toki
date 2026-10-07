#include "Settings.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"

#include <toml++/toml.hpp>

#include <algorithm>
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
        return std::unexpected(std::format("cannot open {} for reading", encodeUtf8(path)));
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad()) {
        return std::unexpected(std::format("cannot read {}", encodeUtf8(path)));
    }
    return std::move(contents).str();
}

[[nodiscard]] std::expected<toml::table, std::string> parseFile(const std::filesystem::path& path) {
    const std::expected<std::string, std::string> contents = readFile(path);
    if (!contents) {
        return std::unexpected(contents.error());
    }

    try {
        return toml::parse(*contents, encodeUtf8(path));
    } catch (const toml::parse_error& error) {
        return std::unexpected(std::format("{}:{}:{}: {}", encodeUtf8(path),
                                           error.source().begin.line, error.source().begin.column,
                                           error.description()));
    }
}

[[nodiscard]] toml::table& ensureSection(toml::table& root, std::string_view name) {
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
        if (const std::optional<LogLevel> parsed = parseLogLevel(*level)) {
            settings.log.minLevel = *parsed;
        } else {
            logWarn("{}: unknown log.min_level '{}', using '{}'", encodeUtf8(path), *level,
                    getLogLevelName(settings.log.minLevel));
        }
    }

    if (const std::optional<s64> maxEntries = (*log)["max_entries"].value<s64>()) {
        if (*maxEntries > 0) {
            settings.log.maxEntries = static_cast<usize>(*maxEntries);
        } else {
            logWarn("{}: log.max_entries must be positive, using {}", encodeUtf8(path),
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

[[nodiscard]] std::vector<std::string> readIds(const toml::array& array) {
    std::vector<std::string> ids;
    ids.reserve(array.size());
    for (const toml::node& node : array) {
        if (const std::optional<std::string> id = node.value<std::string>()) {
            ids.push_back(*id);
        }
    }
    return ids;
}

void readPanels(const toml::table& root, Settings& settings) {
    if (const toml::array* open = root["panels"]["open"].as_array()) {
        settings.openPanels = readIds(*open);
    }
    if (const toml::array* closed = root["panels"]["closed"].as_array()) {
        settings.closedPanels = readIds(*closed);
    }
}

void readBuild(const toml::table& root, const std::filesystem::path& path, Settings& settings) {
    const toml::table* build = root["build"].as_table();
    if (build == nullptr) {
        return;
    }

    for (const auto& [key, node] : *build) {
        if (const toml::value<std::string>* text = node.as_string()) {
            settings.build.insert_or_assign(std::string(key.str()), text->get());
        } else if (const toml::value<s64>* number = node.as_integer()) {
            settings.build.insert_or_assign(std::string(key.str()), std::to_string(number->get()));
        } else {
            logWarn("{}: build.{} must be a string or a number, ignoring it", encodeUtf8(path),
                    key.str());
        }
    }
}

template <typename Node>
[[nodiscard]] std::optional<f32> readFloat(const Node& node) {
    if (const std::optional<f64> value = node.template value<f64>()) {
        return static_cast<f32>(*value);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<CameraPose> readCamera(const toml::table& camera) {
    const toml::array* target = camera["target"].as_array();
    const std::optional<f32> yaw = readFloat(camera["yaw"]);
    const std::optional<f32> pitch = readFloat(camera["pitch"]);
    const std::optional<f32> distance = readFloat(camera["distance"]);
    if (target == nullptr || target->size() != 3 || !yaw || !pitch || !distance) {
        return std::nullopt;
    }
    CameraPose pose{.yaw = *yaw, .pitch = *pitch, .distance = *distance};
    for (usize axis = 0; axis < pose.target.size(); ++axis) {
        const std::optional<f32> value = readFloat((*target)[axis]);
        if (!value) {
            return std::nullopt;
        }
        pose.target[axis] = *value;
    }
    return pose;
}

void readSession(const toml::table& root, Settings& settings) {
    const toml::table* session = root["session"].as_table();
    if (session == nullptr) {
        return;
    }
    const std::optional<std::string> scene = (*session)["scene"].value<std::string>();
    if (!scene) {
        return;
    }
    SceneSession read{.directory = decodeUtf8(*scene)};
    read.layer = static_cast<usize>(std::max<s64>((*session)["layer"].value_or(s64{0}), 0));
    read.lightSetting =
        static_cast<usize>(std::max<s64>((*session)["light_setting"].value_or(s64{0}), 0));
    read.marksActors = (*session)["marks_actors"].value_or(true);
    read.drawsModels = (*session)["draws_models"].value_or(true);
    if (const toml::array* hidden = (*session)["hidden_rooms"].as_array()) {
        for (const toml::node& room : *hidden) {
            if (const std::optional<s64> index = room.value<s64>(); index && *index >= 0) {
                read.hiddenRooms.push_back(static_cast<usize>(*index));
            }
        }
    }
    if (const toml::table* camera = (*session)["camera"].as_table()) {
        read.camera = readCamera(*camera);
    }
    settings.session = std::move(read);
}

void writeSession(toml::table& root, const Settings& settings) {
    if (!settings.session) {
        root.erase("session");
        return;
    }
    const SceneSession& session = *settings.session;
    toml::table written;
    written.insert_or_assign("scene", encodeUtf8(session.directory));
    written.insert_or_assign("layer", static_cast<s64>(session.layer));
    written.insert_or_assign("light_setting", static_cast<s64>(session.lightSetting));
    written.insert_or_assign("marks_actors", session.marksActors);
    written.insert_or_assign("draws_models", session.drawsModels);
    toml::array hidden;
    for (const usize room : session.hiddenRooms) {
        hidden.push_back(static_cast<s64>(room));
    }
    written.insert_or_assign("hidden_rooms", std::move(hidden));
    if (session.camera) {
        toml::array target;
        for (const f32 value : session.camera->target) {
            target.push_back(static_cast<f64>(value));
        }
        toml::table camera;
        camera.insert_or_assign("target", std::move(target));
        camera.insert_or_assign("yaw", static_cast<f64>(session.camera->yaw));
        camera.insert_or_assign("pitch", static_cast<f64>(session.camera->pitch));
        camera.insert_or_assign("distance", static_cast<f64>(session.camera->distance));
        written.insert_or_assign("camera", std::move(camera));
    }
    root.insert_or_assign("session", std::move(written));
}

void writeBuild(toml::table& root, const Settings& settings) {
    if (settings.build.empty()) {
        root.erase("build");
        return;
    }

    toml::table build;
    for (const auto& [name, value] : settings.build) {
        build.insert_or_assign(name, value);
    }
    root.insert_or_assign("build", std::move(build));
}

void writeLog(toml::table& root, const Settings& settings) {
    toml::table& log = ensureSection(root, "log");
    log.insert_or_assign("min_level", std::string{getLogLevelName(settings.log.minLevel)});
    log.insert_or_assign("max_entries", static_cast<s64>(settings.log.maxEntries));
}

void writeWindow(toml::table& root, const Settings& settings) {
    if (!settings.window) {
        root.erase("window");
        return;
    }

    toml::table& window = ensureSection(root, "window");
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
    toml::array closed;
    for (const std::string& id : settings.closedPanels) {
        closed.push_back(id);
    }
    toml::table& panels = ensureSection(root, "panels");
    panels.insert_or_assign("open", std::move(open));
    panels.insert_or_assign("closed", std::move(closed));
}

} // namespace

bool isSamePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::filesystem::path left = first.lexically_normal();
    std::filesystem::path right = second.lexically_normal();
    return left.make_preferred() == right.make_preferred();
}

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
        settings.hackerootPath = decodeUtf8(*checkout);
    }

    readLog(*root, path, settings);
    readWindow(*root, settings);
    readPanels(*root, settings);
    readBuild(*root, path, settings);
    readSession(*root, settings);

    return settings;
}

std::expected<void, std::string> saveSettings(const Settings& settings,
                                              const std::filesystem::path& path) {
    std::error_code status;
    if (const std::filesystem::path parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, status);
        if (status) {
            return std::unexpected(
                std::format("cannot create {}: {}", encodeUtf8(parent), status.message()));
        }
    }

    toml::table root;
    if (std::filesystem::is_regular_file(path, status)) {
        if (std::expected<toml::table, std::string> existing = parseFile(path)) {
            root = std::move(*existing);
        }
    }

    if (settings.hackerootPath) {
        root.insert_or_assign("hackeroot_path", encodeUtf8(*settings.hackerootPath));
    } else {
        root.erase("hackeroot_path");
    }
    writeLog(root, settings);
    writeWindow(root, settings);
    writePanels(root, settings);
    writeBuild(root, settings);
    writeSession(root, settings);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return std::unexpected(std::format("cannot open {} for writing", encodeUtf8(path)));
    }

    stream << root << '\n';
    if (!stream.good()) {
        return std::unexpected(std::format("cannot write {}", encodeUtf8(path)));
    }
    return {};
}

} // namespace toki::core
