#include "Settings.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"
#include "app/viewport/domain/RenderMode.h"
#include "app/viewport/domain/ViewportView.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
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

constexpr auto MarkerShapeNames = std::to_array<std::string_view>({"cube", "diamond", "pin"});
constexpr auto AngleFormatNames = std::to_array<std::string_view>({"binary", "degrees"});

static_assert(AngleFormatNames.size() == AngleFormatCount,
              "every angle format needs a name in the config");

static_assert(MarkerShapeNames.size() == MarkerShapeCount,
              "a marker shape was added to the enum without a name");

constexpr auto ShadingNames = std::to_array<std::string_view>(
    {"n64", "shade_only", "texture_only", "normals", "depth", "wireframe"});
constexpr auto TextureFilterNames = std::to_array<std::string_view>({"n64", "point"});

static_assert(ShadingNames.size() == viewport::ShadingCount,
              "a shading was added to the enum without a name");
static_assert(TextureFilterNames.size() == viewport::TextureFilterCount,
              "a texture filter was added to the enum without a name");

template <typename Enum, usize Count>
[[nodiscard]] Enum readEnum(const toml::node_view<const toml::node> node,
                            const std::array<std::string_view, Count>& names, const Enum fallback) {
    const std::optional<std::string_view> name = node.value<std::string_view>();
    if (!name) {
        return fallback;
    }
    const auto found = std::ranges::find(names, *name);
    return found == names.end() ? fallback : static_cast<Enum>(found - names.begin());
}

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

void readMarkers(const toml::table& root, const std::filesystem::path& path, Settings& settings) {
    const std::optional<std::string> shape = root["markers"]["shape"].value<std::string>();
    if (!shape) {
        return;
    }
    if (const std::optional<MarkerShape> parsed = parseMarkerShape(*shape)) {
        settings.markerShape = *parsed;
    } else {
        logWarn("{}: unknown markers.shape '{}', using '{}'", encodeUtf8(path), *shape,
                getMarkerShapeName(settings.markerShape));
    }
}

void readScenes(const toml::table& root, const std::filesystem::path& path, Settings& settings) {
    if (const std::optional<bool> reviews = root["scenes"]["review_saves"].value<bool>()) {
        settings.reviewsSceneSaves = *reviews;
    }
    const std::optional<std::string> angles = root["scenes"]["angles"].value<std::string>();
    if (!angles) {
        return;
    }
    if (const std::optional<AngleFormat> parsed = parseAngleFormat(*angles)) {
        settings.angleFormat = *parsed;
    } else {
        logWarn("{}: unknown scenes.angles '{}', using '{}'", encodeUtf8(path), *angles,
                getAngleFormatName(settings.angleFormat));
    }
}

void writeScenes(toml::table& root, const Settings& settings) {
    toml::table& scenes = ensureSection(root, "scenes");
    scenes.insert_or_assign("review_saves", settings.reviewsSceneSaves);
    scenes.insert_or_assign("angles", std::string{getAngleFormatName(settings.angleFormat)});
}

void readMessages(const toml::table& root, Settings& settings) {
    if (const std::optional<bool> reviews = root["messages"]["review_saves"].value<bool>()) {
        settings.reviewsMessageSaves = *reviews;
    }
}

void writeMessages(toml::table& root, const Settings& settings) {
    ensureSection(root, "messages").insert_or_assign("review_saves", settings.reviewsMessageSaves);
}

void readKeys(const toml::table& root, const std::filesystem::path& path, Settings& settings) {
    const toml::table* keys = root["keys"].as_table();
    if (keys == nullptr) {
        return;
    }
    for (const auto& [id, value] : *keys) {
        const std::optional<Action> action = findAction(id.str());
        const std::optional<std::string> text = value.value<std::string>();
        const std::optional<KeyChord> chord =
            text ? parseKeyChord(*text) : std::optional<KeyChord>();
        if (!action) {
            logWarn("{}: keys.{} binds no action Toki has", encodeUtf8(path), id.str());
        } else if (!chord) {
            logWarn("{}: keys.{} is not a key such as \"Ctrl+S\"", encodeUtf8(path), id.str());
        } else {
            settings.keyBindings.setChord(*action, *chord);
        }
    }
}

void writeKeys(toml::table& root, const Settings& settings) {
    toml::table& keys = ensureSection(root, "keys");
    for (const ActionInfo& info : getActions()) {
        if (settings.keyBindings.isDefault(info.action)) {
            keys.erase(info.id);
        } else {
            keys.insert_or_assign(info.id,
                                  formatKeyChord(settings.keyBindings.getChord(info.action)));
        }
    }
    if (keys.empty()) {
        root.erase("keys");
    }
}

void writeMarkers(toml::table& root, const Settings& settings) {
    ensureSection(root, "markers")
        .insert_or_assign("shape", std::string{getMarkerShapeName(settings.markerShape)});
}

void readViewport(const toml::table& root, Settings& settings) {
    const toml::table* viewport = root["viewport"].as_table();
    if (viewport == nullptr) {
        return;
    }
    viewport::RenderMode& mode = settings.viewport.renderMode;
    mode.shading = readEnum((*viewport)["shading"], ShadingNames, mode.shading);
    mode.lighting = (*viewport)["lighting"].value_or(mode.lighting);
    mode.fog = (*viewport)["fog"].value_or(mode.fog);
    mode.culling = (*viewport)["culling"].value_or(mode.culling);
    mode.alphaCutout = (*viewport)["alpha_cutout"].value_or(mode.alphaCutout);
    mode.translucency = (*viewport)["translucency"].value_or(mode.translucency);
    mode.textureFilter =
        readEnum((*viewport)["texture_filter"], TextureFilterNames, mode.textureFilter);
    viewport::ViewportOverlays& overlays = settings.viewport.overlays;
    overlays.isShown = (*viewport)["overlays"].value_or(overlays.isShown);
    overlays.flagLinks = (*viewport)["flag_links"].value_or(overlays.flagLinks);
    overlays.grid = (*viewport)["grid"].value_or(overlays.grid);
    overlays.xray = (*viewport)["xray"].value_or(overlays.xray);
    overlays.selectionHighlight =
        (*viewport)["selection_highlight"].value_or(overlays.selectionHighlight);
    overlays.selectionBox = (*viewport)["selection_box"].value_or(overlays.selectionBox);
}

void writeViewport(toml::table& root, const Settings& settings) {
    const viewport::RenderMode& mode = settings.viewport.renderMode;
    const viewport::ViewportOverlays& overlays = settings.viewport.overlays;
    toml::table viewport;
    viewport.insert_or_assign("shading", ShadingNames[static_cast<usize>(mode.shading)]);
    viewport.insert_or_assign("lighting", mode.lighting);
    viewport.insert_or_assign("fog", mode.fog);
    viewport.insert_or_assign("culling", mode.culling);
    viewport.insert_or_assign("alpha_cutout", mode.alphaCutout);
    viewport.insert_or_assign("translucency", mode.translucency);
    viewport.insert_or_assign("texture_filter",
                              TextureFilterNames[static_cast<usize>(mode.textureFilter)]);
    viewport.insert_or_assign("overlays", overlays.isShown);
    viewport.insert_or_assign("flag_links", overlays.flagLinks);
    viewport.insert_or_assign("grid", overlays.grid);
    viewport.insert_or_assign("xray", overlays.xray);
    viewport.insert_or_assign("selection_highlight", overlays.selectionHighlight);
    viewport.insert_or_assign("selection_box", overlays.selectionBox);
    root.insert_or_assign("viewport", std::move(viewport));
}

void readActors(const toml::table& root, Settings& settings) {
    if (const toml::array* dismissed = root["actors"]["dismissed_new"].as_array()) {
        settings.dismissedNewActors = readIds(*dismissed);
    }
}

void writeActors(toml::table& root, const Settings& settings) {
    if (settings.dismissedNewActors.empty()) {
        root.erase("actors");
        return;
    }
    toml::array dismissed;
    for (const std::string& actor : settings.dismissedNewActors) {
        dismissed.push_back(actor);
    }
    ensureSection(root, "actors").insert_or_assign("dismissed_new", std::move(dismissed));
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

void readRun(const toml::table& root, Settings& settings) {
    if (const std::optional<std::string> emulator = root["run"]["emulator"].value<std::string>()) {
        settings.emulatorPath = decodeUtf8(*emulator);
    }
}

void writeRun(toml::table& root, const Settings& settings) {
    if (settings.emulatorPath) {
        ensureSection(root, "run").insert_or_assign("emulator", encodeUtf8(*settings.emulatorPath));
    } else if (toml::table* run = root["run"].as_table()) {
        run->erase("emulator");
        if (run->empty()) {
            root.erase("run");
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
    read.showsPaths = (*session)["shows_paths"].value_or(true);
    read.showsCollision = (*session)["shows_collision"].value_or(false);
    read.showsWaterBoxes = (*session)["shows_waterboxes"].value_or(true);
    read.collisionColoring = (*session)["collision_color"].value_or(std::string());
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
    written.insert_or_assign("shows_paths", session.showsPaths);
    written.insert_or_assign("shows_collision", session.showsCollision);
    written.insert_or_assign("shows_waterboxes", session.showsWaterBoxes);
    if (!session.collisionColoring.empty()) {
        written.insert_or_assign("collision_color", session.collisionColoring);
    }
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

std::string_view getMarkerShapeName(const MarkerShape shape) {
    return MarkerShapeNames[static_cast<usize>(shape)];
}

std::string_view getAngleFormatName(const AngleFormat format) {
    return AngleFormatNames[static_cast<usize>(format)];
}

std::optional<AngleFormat> parseAngleFormat(const std::string_view name) {
    for (usize i = 0; i < AngleFormatCount; ++i) {
        const auto format = static_cast<AngleFormat>(i);
        if (getAngleFormatName(format) == name) {
            return format;
        }
    }
    return std::nullopt;
}

std::optional<MarkerShape> parseMarkerShape(const std::string_view name) {
    for (usize i = 0; i < MarkerShapeCount; ++i) {
        const auto shape = static_cast<MarkerShape>(i);
        if (getMarkerShapeName(shape) == name) {
            return shape;
        }
    }
    return std::nullopt;
}

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
    readRun(*root, settings);
    readSession(*root, settings);
    readActors(*root, settings);
    readMarkers(*root, path, settings);
    readScenes(*root, path, settings);
    readMessages(*root, settings);
    readKeys(*root, path, settings);
    readViewport(*root, settings);

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
    writeViewport(root, settings);
    writeWindow(root, settings);
    writePanels(root, settings);
    writeBuild(root, settings);
    writeRun(root, settings);
    writeSession(root, settings);
    writeActors(root, settings);
    writeMarkers(root, settings);
    writeScenes(root, settings);
    writeMessages(root, settings);
    writeKeys(root, settings);

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
