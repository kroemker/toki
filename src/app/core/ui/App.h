#pragma once

#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/ui/ActorsPanel.h"
#include "app/assets/domain/AssetReader.h"
#include "app/build/domain/BuildRunner.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/PanelManager.h"
#include "app/core/ui/SettingsWindow.h"
#include "app/core/ui/SetupWizard.h"
#include "app/core/ui/Window.h"
#include "app/object/domain/ObjectLoader.h"
#include "app/object/ui/ObjectPanel.h"
#include "app/scene/domain/SceneActorLoader.h"
#include "app/scene/domain/SceneLoader.h"
#include "app/scene/ui/ScenePanel.h"
#include "app/texture/ui/TexturePanel.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "gfx/Device.h"
#include "hackeroot/BuildCommand.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace toki::core {

class App {
public:
    App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
    ~App() = default;

    [[nodiscard]] std::expected<void, std::string> initialize();

    void run();

private:
    void registerPanels();
    void restoreLayout();
    void drawFrame();
    void setHackerootPath(const std::filesystem::path& path);
    void openCheckout(const std::filesystem::path& path);
    void startBuild(hackeroot::BuildTarget target);
    void updateBuild();
    void updateAssets();
    void updateScene();
    void updateSceneActors();
    void updateObject();
    void updateActorDatabase();
    void openRestoredScene();
    void applySettings(const Settings& applied);
    void saveState();

    std::unique_ptr<gfx::Device> device;
    std::unique_ptr<Window> window;
    Settings settings;
    std::filesystem::path settingsPath;

    std::string imguiIniPath;

    Fonts fonts;
    actor::DatabaseStore databaseStore;
    actor::SymbolCatalog symbolCatalog;
    PanelManager panels;
    SetupWizard setupWizard{fonts};
    SettingsWindow settingsWindow;
    workspace::WorkspaceService workspaceService;
    build::BuildRunner buildRunner;
    assets::AssetReader assetReader;
    scene::SceneLoader sceneLoader;
    scene::SceneActorLoader sceneActorLoader;
    /// The scene last open, still to be opened again once the checkout is read.
    std::optional<std::filesystem::path> restoringScene;
    object::ObjectLoader objectLoader;
    viewport::ViewportPanel* viewportPanel = nullptr;
    scene::ScenePanel* scenePanel = nullptr;
    actor::ActorsPanel* actorsPanel = nullptr;
    object::ObjectPanel* objectPanel = nullptr;
    texture::TexturePanel* texturePanel = nullptr;
};

} // namespace toki::core
