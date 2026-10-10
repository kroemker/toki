#include "App.h"

#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/assets/domain/AssetReader.h"
#include "app/assets/domain/AssetReport.h"
#include "app/assets/domain/ObjectNames.h"
#include "app/assets/ui/AssetBrowserPanel.h"
#include "app/build/domain/BuildRunner.h"
#include "app/build/ui/BuildPanel.h"
#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/LogPanel.h"
#include "app/core/ui/MenuBar.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Paths.h"
#include "app/core/ui/SettingsWindow.h"
#include "app/core/ui/Theme.h"
#include "app/core/ui/Window.h"
#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectLoader.h"
#include "app/object/ui/ObjectPanel.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneLoader.h"
#include "app/scene/ui/ScenePanel.h"
#include "app/texture/ui/TexturePanel.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/ui/WorkspacePanel.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "hackeroot/Assets.h"
#include "hackeroot/BuildCommand.h"
#include "hackeroot/Checkout.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr const char* WindowTitle = "Toki";
constexpr const char* SettingsFile = "config.toml";
constexpr const char* ImGuiIniFile = "imgui.ini";

[[nodiscard]] bool isFile(const std::filesystem::path& path) {
    std::error_code status;
    return std::filesystem::is_regular_file(path, status);
}

void logRead(const std::span<const base::Diagnostic> diagnostics, const std::string& summary) {
    usize errors = 0;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == base::Severity::Error) {
            ++errors;
            logError("{}", base::describe(diagnostic));
        } else if (diagnostic.severity == base::Severity::Warning) {
            logWarn("{}", base::describe(diagnostic));
        }
    }
    if (errors > 0) {
        logError("{}; {} errors", summary, errors);
    } else {
        logOk("{}", summary);
    }
}

} // namespace

std::expected<void, std::string> App::initialize() {
    if (const std::expected<std::filesystem::path, std::string> dir = findConfigDirectory()) {
        settingsPath = *dir / SettingsFile;
        imguiIniPath = encodeUtf8(*dir / ImGuiIniFile);
        logDebug("config directory: {}", encodeUtf8(*dir));
    } else {
        logError("{} — settings and layout will not be saved", dir.error());
    }

    if (!settingsPath.empty()) {
        std::expected<Settings, std::string> loaded = loadSettings(settingsPath);
        if (loaded) {
            settings = std::move(*loaded);
        } else {
            logError("{} — starting from defaults", loaded.error());
        }
    }
    log().setMaxEntries(settings.log.maxEntries);

    std::expected<gfx::Device, base::Diagnostic> createdDevice = gfx::Device::create();
    if (!createdDevice) {
        return std::unexpected(base::describe(createdDevice.error()));
    }
    device = std::make_unique<gfx::Device>(std::move(*createdDevice));

    std::expected<Window, std::string> created =
        Window::create(WindowTitle, settings.window, *device);
    if (!created) {
        return std::unexpected(created.error());
    }
    window = std::make_unique<Window>(std::move(*created));

    ImGui::GetIO().IniFilename = imguiIniPath.empty() ? nullptr : imguiIniPath.c_str();
    applyTheme();

    if (const std::expected<std::filesystem::path, std::string> assets = findAssetDirectory()) {
        logDebug("asset directory: {}", encodeUtf8(*assets));
        fonts = Fonts::load(*assets);
    } else {
        logError("{} — falling back to the built-in font", assets.error());
        fonts = Fonts::load(std::filesystem::path{});
    }

    registerPanels();
    restoreLayout();

    if (settings.hackerootPath) {
        openCheckout(*settings.hackerootPath);
        if (settings.session && scenePanel != nullptr) {
            restoringScene = settings.session->directory;
            scenePanel->restoreSession(*settings.session);
        }
    } else {
        setupWizard.beginFirstRun();
    }

    logInfo("Toki ready");
    return {};
}

void App::registerPanels() {
    auto viewport = std::make_unique<viewport::ViewportPanel>(*device);
    viewportPanel = viewport.get();
    auto sceneView =
        std::make_unique<scene::ScenePanel>(workspaceService, *viewport, databaseStore);
    scenePanel = sceneView.get();
    auto textures = std::make_unique<texture::TexturePanel>(*device);
    texturePanel = textures.get();
    auto objectView = std::make_unique<object::ObjectPanel>(*viewport, *textures);
    objectPanel = objectView.get();
    auto actors =
        std::make_unique<actor::ActorsPanel>(workspaceService, databaseStore, symbolCatalog);
    actorsPanel = actors.get();
    scenePanel->setActorEditor([this](const std::string& actor) { actorsPanel->openActor(actor); });
    panels.add(std::move(viewport));
    panels.add(std::move(sceneView));
    panels.add(std::move(objectView));
    panels.add(std::move(textures));
    panels.add(std::move(actors));
    panels.add(std::make_unique<LogPanel>(log(), settings.log, fonts));
    panels.add(std::make_unique<assets::AssetBrowserPanel>(
        workspaceService, assetReader,
        [this](const hackeroot::AssetDirectory& asset) {
            objectLoader.close();
            sceneActorLoader.close();
            sceneLoader.open(workspaceService.getSharedWorkspace(), asset);
        },
        [this](const hackeroot::AssetDirectory& asset) {
            sceneLoader.close();
            sceneActorLoader.close();
            restoringScene.reset();
            objectLoader.open(workspaceService.getSharedWorkspace(), asset);
        }));
    panels.add(std::make_unique<workspace::WorkspacePanel>(workspaceService));
    panels.add(std::make_unique<build::BuildPanel>(
        buildRunner, fonts, [this](const hackeroot::BuildTarget target) { startBuild(target); }));
}

void App::restoreLayout() {
    if (settings.openPanels) {
        panels.restoreOpenState(*settings.openPanels, settings.closedPanels);
    } else {
        panels.openDefaults();
    }

    if (imguiIniPath.empty() || !isFile(decodeUtf8(imguiIniPath))) {
        panels.requestDefaultLayout();
    }
}

void App::run() {
    while (window->beginFrame()) {
        drawFrame();
        window->endFrame();
    }
    saveState();
}

void App::drawFrame() {
    workspaceService.update();
    openRestoredScene();
    updateBuild();
    updateAssets();
    updateScene();
    updateSceneActors();
    updateObject();
    updateActorDatabase();
    if (viewportPanel != nullptr) {
        std::optional<std::string> reading = sceneLoader.findReadingName();
        if (!reading) {
            reading = sceneActorLoader.findReadingName();
        }
        if (!reading) {
            reading = objectLoader.findReadingName();
        }
        viewportPanel->setReadingName(std::move(reading));
    }
    const MenuActions actions = drawMenuBar(panels, settings, buildRunner.isRunning());
    panels.draw();

    if (const std::optional<std::filesystem::path> chosen = setupWizard.draw(window->getHandle())) {
        setHackerootPath(*chosen);
    }

    if (actions.openSettings ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Comma, ImGuiInputFlags_RouteGlobal)) {
        settingsWindow.open(settings);
    }
    const SettingsActions edited = settingsWindow.draw();
    if (edited.applied) {
        applySettings(*edited.applied);
    }

    if ((actions.changeHackerootPath || edited.changeHackerootPath) && !setupWizard.isActive()) {
        setupWizard.beginChangePath(settings.hackerootPath);
    }
    if (actions.build || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_B, ImGuiInputFlags_RouteGlobal)) {
        startBuild(hackeroot::BuildTarget::Build);
    }
    if (actions.run || ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal)) {
        startBuild(hackeroot::BuildTarget::Run);
    }
    if (actions.stopBuild) {
        buildRunner.stop();
    }
    if (actions.quit) {
        window->requestClose();
    }
}

void App::startBuild(const hackeroot::BuildTarget target) {
    if (buildRunner.isRunning()) {
        return;
    }
    if (!settings.hackerootPath) {
        logError("set the HackerOoT folder before building");
        return;
    }
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(*settings.hackerootPath);
    if (!root) {
        logError("{}", base::describe(root.error()));
        return;
    }
    const std::expected<void, base::Diagnostic> started = buildRunner.start(
        *root, makeparser::MakeOverrides(settings.build.begin(), settings.build.end()), target);
    if (!started) {
        logError("{}", base::describe(started.error()));
        return;
    }
    if (target == hackeroot::BuildTarget::Run) {
        logInfo("building and running {}", encodeUtf8(*root));
    } else {
        logInfo("building {}", encodeUtf8(*root));
    }
    if (Panel* panel = panels.find("build")) {
        panel->setOpen(true);
    }
}

void App::updateAssets() {
    const std::optional<assets::AssetReport> report = assetReader.update();
    if (!report) {
        return;
    }
    for (const assets::FileReport& file : report->files) {
        logDebug("{}: {} declarations, {} images", encodeUtf8(file.file.filename()),
                 file.declarations, file.images);
        for (const base::Diagnostic& diagnostic : file.diagnostics) {
            switch (diagnostic.severity) {
            case base::Severity::Note:
                logDebug("{}", base::describe(diagnostic));
                break;
            case base::Severity::Warning:
                logWarn("{}", base::describe(diagnostic));
                break;
            case base::Severity::Error:
                logError("{}", base::describe(diagnostic));
                break;
            }
        }
    }
    const assets::ReportCounts counts = assets::countReport(*report);
    const std::string summary = std::format(
        "opened {} in {} ms: {} files, {} declarations, {} images; {} errors, {} warnings, "
        "{} notes",
        report->name, report->elapsed.count(), counts.files, counts.declarations, counts.images,
        counts.errors, counts.warnings, counts.notes);
    if (counts.errors > 0) {
        logError("{}", summary);
    } else if (counts.warnings > 0) {
        logWarn("{}", summary);
    } else {
        logOk("{}", summary);
    }
}

void App::updateScene() {
    std::optional<scene::Scene> loaded = sceneLoader.update();
    if (!loaded) {
        return;
    }
    const usize rooms = loaded->layers.empty() ? 0 : loaded->layers.front().rooms.size();
    logRead(loaded->diagnostics,
            std::format("read {} in {} ms: {} rooms, {} layers", loaded->name,
                        loaded->elapsed.count(), rooms, loaded->layers.size()));
    if (objectPanel != nullptr) {
        objectPanel->clearObject();
    }
    if (texturePanel != nullptr) {
        std::vector<texture::TextureGroup> groups;
        for (usize i = scene::SceneImage; i < loaded->images.size(); ++i) {
            groups.push_back(texture::TextureGroup{
                .label = i == scene::SceneImage ? std::string("Scene")
                                                : std::format("Room {}", i - scene::FirstRoomImage),
                .image = loaded->images[i]});
        }
        texturePanel->setTextures(loaded->name, std::move(groups));
    }
    const std::expected<std::filesystem::path, std::string> assets = findAssetDirectory();
    sceneActorLoader.open(
        workspaceService.getSharedWorkspace(),
        scene::ActorRequest{.name = std::format("{}'s actors", loaded->name),
                            .scene = loaded->name,
                            .layers = loaded->layers,
                            .shippedDatabase = assets ? actor::makeShippedDatabasePath(*assets)
                                                      : std::filesystem::path()});
    if (scenePanel != nullptr) {
        scenePanel->setScene(std::move(*loaded));
    }
}

void App::updateActorDatabase() {
    if (const std::shared_ptr<const hackeroot::Workspace> workspace =
            workspaceService.getSharedWorkspace()) {
        symbolCatalog.open(workspace);
        if (const std::expected<std::filesystem::path, std::string> assets = findAssetDirectory()) {
            databaseStore.open(actor::DatabaseFiles{
                .shipped = actor::makeShippedDatabasePath(*assets),
                .checkout = actor::makeCheckoutDatabasePath(workspace->getRoot())});
        }
    }
    symbolCatalog.update();
    for (const std::string& failure : databaseStore.takeFailures()) {
        logError("{}", failure);
    }
    if (std::optional<actor::DatabaseSnapshot> snapshot = databaseStore.update();
        snapshot && actorsPanel != nullptr) {
        actorsPanel->setDatabases(std::move(*snapshot));
    }
}

void App::updateSceneActors() {
    std::optional<scene::SceneActors> loaded = sceneActorLoader.update();
    if (!loaded) {
        return;
    }
    const auto drawn = std::ranges::count_if(
        loaded->models, [](const auto& entry) { return entry.second.hasDrawing(); });
    logRead(loaded->diagnostics,
            std::format(
                "read the scene's actors in {} ms ({} ms overlays, {} ms objects): {} of {} drawn "
                "as models, from {} objects",
                loaded->elapsed.count(), loaded->overlaysElapsed.count(),
                loaded->objectsElapsed.count(), drawn, loaded->models.size(),
                loaded->objects.size()));
    if (scenePanel != nullptr) {
        scenePanel->setActors(std::move(*loaded));
    }
}

void App::updateObject() {
    std::optional<object::Object> loaded = objectLoader.update();
    if (!loaded) {
        return;
    }
    logRead(loaded->diagnostics, std::format("read {} in {} ms: {} assets", loaded->name,
                                             loaded->elapsed.count(), loaded->assets.size()));
    if (scenePanel != nullptr) {
        scenePanel->clearScene();
    }
    if (texturePanel != nullptr && loaded->images.empty()) {
        texturePanel->clearTextures();
    } else if (texturePanel != nullptr) {
        texturePanel->setTextures(
            std::string(assets::findObjectName(loaded->name).value_or(loaded->name)),
            {texture::TextureGroup{.label = loaded->name,
                                   .image = loaded->images[loaded->objectImage]}});
    }
    if (objectPanel != nullptr) {
        objectPanel->setObject(std::move(*loaded));
    }
}

void App::openRestoredScene() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (!restoringScene || workspace == nullptr) {
        return;
    }
    const std::vector<hackeroot::AssetDirectory>& scenes = workspace->getAssets().scenes;
    const auto found = std::ranges::find_if(scenes, [this](const hackeroot::AssetDirectory& scene) {
        return isSamePath(scene.directory, *restoringScene);
    });
    if (found == scenes.end()) {
        logWarn("the scene open last time, {}, is not in the checkout any more",
                encodeUtf8(*restoringScene));
    } else {
        logInfo("opening {} again, as it was left", found->name);
        sceneLoader.open(workspaceService.getSharedWorkspace(), *found);
    }
    restoringScene.reset();
}

void App::applySettings(const Settings& applied) {
    const bool rebuildsCheckout = applied.build != settings.build;
    settings.build = applied.build;
    settings.log = applied.log;
    log().setMaxEntries(settings.log.maxEntries);
    saveState();
    if (rebuildsCheckout && settings.hackerootPath) {
        logInfo("build settings changed, reading the checkout again");
        openCheckout(*settings.hackerootPath);
    }
}

void App::updateBuild() {
    const std::optional<build::BuildOutcome> outcome = buildRunner.update();
    if (!outcome) {
        return;
    }
    if (outcome->status == build::BuildStatus::Succeeded) {
        logOk("{}", outcome->summary);
    } else if (outcome->status == build::BuildStatus::Stopped) {
        logWarn("{}", outcome->summary);
    } else {
        logError("{}", outcome->summary);
    }
}

void App::setHackerootPath(const std::filesystem::path& path) {
    settings.hackerootPath = path;
    logOk("HackerOoT folder set to {}", encodeUtf8(path));

    saveState();
    openCheckout(path);
}

void App::openCheckout(const std::filesystem::path& path) {
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(path);
    if (!root) {
        logError("{}", base::describe(root.error()));
        return;
    }
    logInfo("reading the checkout at {}", encodeUtf8(*root));
    workspaceService.open(*root,
                          makeparser::MakeOverrides(settings.build.begin(), settings.build.end()));
}

void App::saveState() {
    if (!imguiIniPath.empty()) {
        ImGui::SaveIniSettingsToDisk(imguiIniPath.c_str());
    }

    if (settingsPath.empty()) {
        return;
    }

    settings.window = window->getGeometry();
    settings.openPanels = panels.getOpenIds();
    settings.closedPanels = panels.getClosedIds();
    if (std::optional<SceneSession> session =
            scenePanel != nullptr ? scenePanel->makeSession() : std::nullopt) {
        settings.session = std::move(session);
    } else if (!restoringScene) {
        settings.session.reset();
    }

    if (const std::expected<void, std::string> saved = saveSettings(settings, settingsPath);
        !saved) {
        logError("{}", saved.error());
    }
}

} // namespace toki::core
