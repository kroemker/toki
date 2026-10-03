#include "App.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/LogPanel.h"
#include "app/core/ui/MenuBar.h"
#include "app/core/ui/Paths.h"
#include "app/core/ui/Theme.h"
#include "app/core/ui/Window.h"
#include "app/workspace/ui/WorkspacePanel.h"
#include "base/Diagnostic.h"
#include "hackeroot/Checkout.h"

#include <imgui.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace toki::core {

namespace {

constexpr const char* WindowTitle = "Toki";
constexpr const char* SettingsFile = "config.toml";
constexpr const char* ImGuiIniFile = "imgui.ini";

[[nodiscard]] bool isFile(const std::filesystem::path& path) {
    std::error_code status;
    return std::filesystem::is_regular_file(path, status);
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

    std::expected<Window, std::string> created = Window::create(WindowTitle, settings.window);
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
    } else {
        setupWizard.beginFirstRun();
    }

    logInfo("Toki ready");
    return {};
}

void App::registerPanels() {
    panels.add(std::make_unique<LogPanel>(log(), settings.log, fonts));
    panels.add(std::make_unique<workspace::WorkspacePanel>(workspaceService));
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
    const MenuActions actions = drawMenuBar(panels, settings);
    panels.draw();

    if (const std::optional<std::filesystem::path> chosen = setupWizard.draw(window->getHandle())) {
        setHackerootPath(*chosen);
    }

    if (actions.changeHackerootPath && !setupWizard.isActive()) {
        setupWizard.beginChangePath(settings.hackerootPath);
    }
    if (actions.quit) {
        window->requestClose();
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

    if (const std::expected<void, std::string> saved = saveSettings(settings, settingsPath);
        !saved) {
        logError("{}", saved.error());
    }
}

} // namespace toki::core
