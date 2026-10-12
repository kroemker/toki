#pragma once

#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/ui/Dialog.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/tools/configure/ui/MemoryMap.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/BuildMap.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toki::configure {

/// The checkout's configuration headers as settings, a tab for each header:
/// each setting by its documented name, edited by what it is written as, with
/// the value the build ends with beside it when the safeguards override it.
/// Changes are written once their plan has been reviewed.
class ConfigureDialog : public core::Dialog {
public:
    ConfigureDialog(const workspace::WorkspaceService& workspaceService,
                    actor::CollectionCatalog& collectionCatalog);

    void draw() override;

private:
    void updateSettings();
    void requestBuildMap();
    void updateBuildMap();
    void drawGroup(const ConfigGroup& group);
    void drawSetting(const ConfigSetting& setting);
    void drawValue(const ConfigSetting& setting);
    [[nodiscard]] bool drawControl(const ConfigSetting& setting, std::string& value);
    [[nodiscard]] bool drawChoice(const ConfigSetting& setting, std::string& value);

    const workspace::WorkspaceService& workspaceService;
    actor::CollectionCatalog& collectionCatalog;
    const hackeroot::Workspace* listedWorkspace = nullptr;
    bool isStale = true;
    std::filesystem::path readingRoot;
    std::filesystem::path groupsRoot;
    std::future<std::vector<hackeroot::ConfigDefine>> reading;
    std::vector<ConfigGroup> groups;
    std::map<std::string, std::string, std::less<>> values;
    std::string choiceFilter;
    std::future<std::expected<hackeroot::BuildMap, std::string>> mapReading;
    std::optional<std::expected<hackeroot::BuildMap, std::string>> buildMap;
    bool isMemoryShown = false;
    MemoryMap memoryMap;
    tools::EditFlow editFlow;
};

} // namespace toki::configure
