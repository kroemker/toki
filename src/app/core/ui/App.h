#pragma once

#include "app/assets/domain/AssetReader.h"
#include "app/build/domain/BuildRunner.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/PanelManager.h"
#include "app/core/ui/SetupWizard.h"
#include "app/core/ui/Window.h"
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
    void saveState();

    std::unique_ptr<gfx::Device> device;
    std::unique_ptr<Window> window;
    Settings settings;
    std::filesystem::path settingsPath;

    std::string imguiIniPath;

    Fonts fonts;
    PanelManager panels;
    SetupWizard setupWizard{fonts};
    workspace::WorkspaceService workspaceService;
    build::BuildRunner buildRunner;
    assets::AssetReader assetReader;
};

} // namespace toki::core
