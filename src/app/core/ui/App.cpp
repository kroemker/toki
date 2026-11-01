#include "App.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/DatabaseEdits.h"
#include "app/actor/domain/Derivation.h"
#include "app/assets/domain/AssetReader.h"
#include "app/assets/domain/AssetReport.h"
#include "app/assets/domain/ObjectNames.h"
#include "app/assets/ui/AssetBrowserPanel.h"
#include "app/build/domain/BuildRunner.h"
#include "app/build/domain/BuildTarget.h"
#include "app/build/domain/EmulatorCommand.h"
#include "app/build/domain/Process.h"
#include "app/build/ui/BuildPanel.h"
#include "app/core/Types.h"
#include "app/core/domain/BuildChoices.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/LogPanel.h"
#include "app/core/ui/MenuBar.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Paths.h"
#include "app/core/ui/PropertiesPanel.h"
#include "app/core/ui/SettingsWindow.h"
#include "app/core/ui/Theme.h"
#include "app/core/ui/Window.h"
#include "app/displayList/ui/DisplayListPanel.h"
#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectLoader.h"
#include "app/object/ui/ObjectPanel.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneLoader.h"
#include "app/scene/ui/ScenePanel.h"
#include "app/texture/ui/TexturePanel.h"
#include "app/tools/configure/ui/ConfigureDialog.h"
#include "app/tools/createActor/ui/CreateActorDialog.h"
#include "app/tools/registerScene/ui/RegisterSceneDialog.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/ui/WorkspacePanel.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "hackeroot/Assets.h"
#include "hackeroot/BuildDefines.h"
#include "hackeroot/Checkout.h"
#include "hackeroot/Tables.h"
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
    setKeyBindings(settings.keyBindings);

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
    registerDialogs();
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
    viewportPanel->setView(settings.viewport);
    auto sceneView = std::make_unique<scene::ScenePanel>(workspaceService, *viewport, databaseStore,
                                                         collectionCatalog);
    scenePanel = sceneView.get();
    scenePanel->setMarkerShape(settings.markerShape);
    setSceneAngleFormat();
    if (const std::expected<std::filesystem::path, std::string> assets = findAssetDirectory()) {
        scene::CollisionNames collisionNames =
            scene::readCollisionNames(scene::makeShippedCollisionNamesPath(*assets));
        for (const base::Diagnostic& diagnostic : collisionNames.diagnostics) {
            logWarn("{}", base::describe(diagnostic));
        }
        scenePanel->setCollisionNames(std::move(collisionNames));
    }
    auto textures = std::make_unique<texture::TexturePanel>(*device);
    texturePanel = textures.get();
    auto objectView = std::make_unique<object::ObjectPanel>(*viewport, *textures);
    objectPanel = objectView.get();
    auto actors = std::make_unique<actor::ActorsPanel>(workspaceService, databaseStore,
                                                       symbolCatalog, collectionCatalog, fonts);
    actorsPanel = actors.get();
    scenePanel->setActorEditor([this](const std::string& actor) { actorsPanel->openActor(actor); });
    scenePanel->setActorObjectFinder([this](const s16 actorId) -> std::optional<std::string> {
        const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
        const auto index = static_cast<usize>(static_cast<u16>(actorId));
        if (workspace == nullptr || !actorDatabase ||
            index >= workspace->getTables().actors.size()) {
            return std::nullopt;
        }
        const actor::ActorEntry* entry = actor::findEntry(
            actorDatabase->database, workspace->getTables().actors[index].enumName);
        return entry != nullptr ? entry->object : std::nullopt;
    });
    scenePanel->setActorNameFinder(
        [this](const std::string_view enumName) -> std::optional<std::string> {
            const actor::ActorEntry* entry =
                actorDatabase ? actor::findEntry(actorDatabase->database, enumName) : nullptr;
            return entry != nullptr ? std::optional(entry->name) : std::nullopt;
        });
    scenePanel->setActorReader([this](const scene::Scene& edited) {
        sceneActorLoader.open(workspaceService.getSharedWorkspace(),
                              scene::ActorRequest{.name = std::format("{}'s actors", edited.name),
                                                  .scene = edited.name,
                                                  .layers = edited.layers});
    });
    panels.add(std::move(viewport));
    panels.add(std::move(sceneView));
    auto properties = std::make_unique<PropertiesPanel>();
    propertiesPanel = properties.get();
    propertiesPanel->addSource(*scenePanel);
    panels.add(std::move(properties));
    panels.add(std::move(objectView));
    panels.add(std::move(textures));
    panels.add(std::move(actors));
    auto displayList = std::make_unique<displayList::DisplayListPanel>(*device, *viewportPanel,
                                                                       workspaceService, fonts);
    displayListPanel = displayList.get();
    const auto inspect = [this](const std::string& symbol) {
        displayListPanel->inspectSymbol(symbol);
    };
    objectPanel->setDisplayListInspector(inspect);
    scenePanel->setDisplayListInspector(inspect);
    panels.add(std::move(displayList));
    panels.add(std::make_unique<LogPanel>(log(), settings.log, fonts));
    browserBoxes = std::make_unique<message::BoxRenderer>(*device);
    auto messages = std::make_unique<message::MessagePanel>(*device, messageFocus,
                                                            [this] { saveMessagesDialog.open(); });
    messagePanel = messages.get();
    propertiesPanel->addSource(*messagePanel);
    panels.add(std::move(messages));
    messageBrowser = std::make_unique<message::MessageBrowser>(
        messageFocus, *browserBoxes, [this](usize) { messagePanel->showMessage(); });
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
        },
        *messageBrowser));
    panels.add(std::make_unique<workspace::WorkspacePanel>(workspaceService));
    panels.add(std::make_unique<build::BuildPanel>(
        buildRunner, fonts, [this](const build::BuildTarget target) { startBuild(target); },
        [this] { return settings.emulatorPath.has_value(); }));
}

void App::registerDialogs() {
    dialogs.push_back(
        std::make_unique<configure::ConfigureDialog>(workspaceService, collectionCatalog));
    dialogs.push_back(
        std::make_unique<registerScene::RegisterSceneDialog>(workspaceService, collectionCatalog));
    dialogs.push_back(std::make_unique<createActor::CreateActorDialog>(workspaceService,
                                                                       collectionCatalog, fonts));
}

void App::openSavedScene(const std::filesystem::path& directory) {
    if (scenePanel == nullptr || scenePanel->findScene() == nullptr) {
        return;
    }
    scenePanel->markSaved();
    const scene::Scene& saved = *scenePanel->findScene();
    if (core::isSamePath(saved.directory, directory)) {
        return;
    }
    if (std::optional<core::SceneSession> session = scenePanel->makeSession()) {
        session->directory = directory;
        scenePanel->restoreSession(std::move(*session));
    }
    sceneActorLoader.close();
    sceneLoader.open(
        workspaceService.getSharedWorkspace(),
        hackeroot::AssetDirectory{.name = saved.name,
                                  .directory = directory,
                                  .roomCount = saved.images.size() - scene::FirstRoomImage});
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
    updateMessages();
    updateActorDatabase();
    if (viewportPanel != nullptr && displayListPanel != nullptr) {
        viewportPanel->setTracing(displayListPanel->isOpen());
        displayListPanel->updateHighlight();
    }
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
    if (scenePanel != nullptr) {
        scenePanel->setSaving(saveSceneDialog.isSaving());
    }
    const scene::Scene* openScene = scenePanel != nullptr ? scenePanel->findScene() : nullptr;
    const MenuActions actions =
        drawMenuBar(panels, dialogs, settings,
                    MenuState{.buildRunning = buildRunner.isRunning(),
                              .sceneOpen = openScene != nullptr,
                              .canUndo = scenePanel != nullptr && scenePanel->canUndo(),
                              .canRedo = scenePanel != nullptr && scenePanel->canRedo()});
    if (scenePanel != nullptr && (actions.undo || isActionPressed(Action::Undo))) {
        scenePanel->undo();
    }
    if (scenePanel != nullptr && (actions.redo || isActionPressed(Action::Redo))) {
        scenePanel->redo();
    }
    panels.draw();
    if (scenePanel != nullptr) {
        scenePanel->update();
    }
    for (const std::unique_ptr<Dialog>& dialog : dialogs) {
        dialog->drawWindow();
    }
    if (openScene != nullptr && !saveSceneDialog.isSaving() &&
        (actions.saveScene || isActionPressed(Action::SaveScene))) {
        saveSceneDialog.open(*openScene, scenePanel->findEditedModel(), settings.reviewsSceneSaves);
    }
    saveSceneDialog.update();
    saveSceneDialog.drawWindow();
    saveMessagesDialog.drawWindow();
    drawNewActorsPrompt();

    if (const std::optional<std::filesystem::path> chosen = setupWizard.draw(window->getHandle())) {
        setHackerootPath(*chosen);
    }

    if (actions.openSettings || isActionPressed(Action::OpenSettings)) {
        settingsWindow.open(settings, makeBuildChoices());
    }
    const SettingsActions edited = settingsWindow.draw(window->getHandle());
    if (edited.applied) {
        applySettings(*edited.applied);
    }

    if ((actions.changeHackerootPath || edited.changeHackerootPath) && !setupWizard.isActive()) {
        setupWizard.beginChangePath(settings.hackerootPath);
    }
    if (actions.build || isActionPressed(Action::Build)) {
        startBuild(build::BuildTarget::Build);
    }
    if (actions.run || isActionPressed(Action::Run)) {
        startBuild(build::BuildTarget::Run);
    }
    if (actions.stopBuild) {
        buildRunner.stop();
    }
    if (actions.quit) {
        window->requestClose();
    }
}

void App::startBuild(const build::BuildTarget target) {
    if (buildRunner.isRunning()) {
        return;
    }
    if (!settings.hackerootPath) {
        logError("set the HackerOoT folder before building");
        return;
    }
    if (target == build::BuildTarget::Run && !settings.emulatorPath) {
        logError("set the emulator in Settings, under General, to run the ROM");
        return;
    }
    if (target == build::BuildTarget::Run && !isFile(*settings.emulatorPath)) {
        logError("the emulator {} is not a file", encodeUtf8(*settings.emulatorPath));
        return;
    }
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(*settings.hackerootPath);
    if (!root) {
        logError("{}", base::describe(root.error()));
        return;
    }
    const std::expected<void, base::Diagnostic> started = buildRunner.start(
        *root, makeparser::MakeOverrides(settings.build.begin(), settings.build.end()));
    if (!started) {
        logError("{}", base::describe(started.error()));
        return;
    }
    runsAfterBuild = target == build::BuildTarget::Run;
    if (runsAfterBuild) {
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
    sceneActorLoader.open(workspaceService.getSharedWorkspace(),
                          scene::ActorRequest{.name = std::format("{}'s actors", loaded->name),
                                              .scene = loaded->name,
                                              .layers = loaded->layers});
    if (scenePanel != nullptr) {
        scenePanel->setScene(std::move(*loaded));
    }
}

void App::updateMessages() {
    const std::shared_ptr<const hackeroot::Workspace> workspace =
        workspaceService.getSharedWorkspace();
    if (workspace.get() != messageWorkspace) {
        messageWorkspace = workspace.get();
        if (messageFocus.draft.isEdited()) {
            logWarn("the edited messages belong to the checkout closed, and were dropped");
        }
        messageFocus = {};
        if (workspace) {
            messageLoader.open(
                workspace, message::MessageRequest{.name = "the messages", .workspace = workspace});
        } else {
            messageLoader.close();
        }
    }
    std::optional<message::SharedCatalog> loaded = messageLoader.update();
    if (!loaded) {
        return;
    }
    logRead((*loaded)->diagnostics,
            std::format("read {} messages", (*loaded)->table.messages.size()));
    const message::Message* shown = messageFocus.findShown();
    const std::optional<std::pair<u16, message::MessageMacro>> wasShown =
        shown != nullptr ? std::optional(std::pair(shown->id, shown->macro)) : std::nullopt;
    messageFocus.catalog = std::move(*loaded);
    if (!messageFocus.draft.setRead(messageFocus.catalog->table)) {
        logWarn("the checkout's messages changed while some are edited; the edits are kept");
        return;
    }
    messageFocus.shown.reset();
    if (wasShown) {
        const std::vector<message::Message>& messages = messageFocus.draft.getTable().messages;
        const auto found = std::ranges::find_if(messages, [&](const message::Message& candidate) {
            return candidate.id == wasShown->first && candidate.macro == wasShown->second;
        });
        if (found != messages.end()) {
            messageFocus.shown = static_cast<usize>(found - messages.begin());
        }
    }
}

void App::updateActorDatabase() {
    if (const std::shared_ptr<const hackeroot::Workspace> workspace =
            workspaceService.getSharedWorkspace()) {
        symbolCatalog.open(workspace);
        collectionCatalog.open(workspace);
        if (const std::expected<std::filesystem::path, std::string> assets = findAssetDirectory()) {
            databaseStore.open(
                actor::DatabaseFiles{.builtIn = actor::makeShippedDatabasePath(*assets),
                                     .checkout =
                                         actor::makeCheckoutDatabasePath(workspace->getRoot())},
                workspace);
        }
    }
    symbolCatalog.update();
    collectionCatalog.update();
    for (const base::Diagnostic& diagnostic : collectionCatalog.takeDiagnostics()) {
        logWarn("{}", base::describe(diagnostic));
    }
    for (const std::string& failure : databaseStore.takeFailures()) {
        logError("{}", failure);
    }
    std::optional<actor::DatabaseSnapshot> snapshot = databaseStore.update();
    if (snapshot) {
        actorDatabase = *snapshot;
        if (actorsPanel != nullptr) {
            actorsPanel->setDatabase(std::move(*snapshot));
        }
    }
    offerNewActors(snapshot.has_value());
}

void App::offerNewActors(const bool isRead) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr || !actorDatabase || (!isRead && workspace == offeredWorkspace) ||
        databaseStore.getFiles() != actorDatabase->files) {
        return;
    }
    offeredWorkspace = workspace;
    std::vector<std::string> table;
    for (const hackeroot::ActorEntry& actor : workspace->getTables().actors) {
        if (!actor.isUnset) {
            table.push_back(actor.enumName);
        }
    }
    if (table == offeredActorTable) {
        return;
    }
    offeredActorTable = std::move(table);
    newActorsPrompt.offer(actor::listNewActors(workspace->getTables(), actorDatabase->database),
                          settings.dismissedNewActors);
}

void App::drawNewActorsPrompt() {
    const std::optional<actor::NewActorsAnswer> answer = newActorsPrompt.draw();
    if (!answer || !actorDatabase) {
        return;
    }
    if (!answer->isDerived) {
        settings.dismissedNewActors = answer->actors;
        saveState();
        return;
    }
    logInfo("deriving {} new actors into the actor database", answer->actors.size());
    databaseStore.edit([workspace = workspaceService.getSharedWorkspace(),
                        builtIn = actorDatabase->builtIn, actors = answer->actors](
                           actor::ActorDatabase& database) -> std::expected<void, std::string> {
        if (workspace == nullptr) {
            return std::unexpected(std::string("no checkout is open to derive actors from"));
        }
        return actor::fillEntries(database, *workspace, builtIn, actors);
    });
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
                "read the scene's actors in {} ms ({} ms objects): {} of {} drawn as models, from "
                "{} objects",
                loaded->elapsed.count(), loaded->objectsElapsed.count(), drawn,
                loaded->models.size(), loaded->objects.size()));
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

void App::setSceneAngleFormat() {
    scenePanel->setAngleFormat(settings.angleFormat, [this](const AngleFormat format) {
        settings.angleFormat = format;
        saveState();
    });
}

void App::applySettings(const Settings& applied) {
    const bool rebuildsCheckout = applied.build != settings.build;
    settings.build = applied.build;
    settings.emulatorPath = applied.emulatorPath;
    settings.log = applied.log;
    settings.markerShape = applied.markerShape;
    settings.reviewsSceneSaves = applied.reviewsSceneSaves;
    settings.angleFormat = applied.angleFormat;
    settings.keyBindings = applied.keyBindings;
    setKeyBindings(settings.keyBindings);
    log().setMaxEntries(settings.log.maxEntries);
    if (scenePanel != nullptr) {
        scenePanel->setMarkerShape(settings.markerShape);
        setSceneAngleFormat();
    }
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
    if (std::exchange(runsAfterBuild, false) && outcome->status == build::BuildStatus::Succeeded) {
        startEmulator();
    }
}

void App::startEmulator() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (!settings.emulatorPath) {
        logError("set the emulator in Settings, under General, to run the ROM");
        return;
    }
    if (workspace == nullptr) {
        logError("the ROM cannot be run before the checkout is read");
        return;
    }
    const std::filesystem::path& rom = workspace->getBuildDefines().romFile;
    if (rom.empty()) {
        logError("the Makefile names no ROM to run");
        return;
    }
    const std::vector<std::string> command =
        build::makeEmulatorCommand(*settings.emulatorPath, rom);
    if (const std::expected<void, base::Diagnostic> started =
            build::Process::launch(command, settings.emulatorPath->parent_path());
        !started) {
        logError("{}", base::describe(started.error()));
        return;
    }
    logInfo("running {} in {}", encodeUtf8(rom.filename()),
            encodeUtf8(settings.emulatorPath->filename()));
}

BuildChoices App::makeBuildChoices() const {
    BuildChoices choices;
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace == nullptr) {
        return choices;
    }
    choices.versions = workspace->getAssets().versions;
    const hackeroot::BuildDefines& defines = workspace->getBuildDefines();
    if (!settings.build.contains("VERSION")) {
        choices.defaults.emplace("VERSION", defines.version);
    }
    if (!settings.build.contains("DEBUG_FEATURES")) {
        choices.defaults.emplace("DEBUG_FEATURES", defines.debugFeatures);
    }
    return choices;
}

void App::setHackerootPath(const std::filesystem::path& path) {
    settings.hackerootPath = path;
    settings.dismissedNewActors.clear();
    offeredActorTable.clear();
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
    if (viewportPanel != nullptr) {
        settings.viewport = viewportPanel->getView();
    }
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
