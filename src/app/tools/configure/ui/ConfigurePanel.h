#pragma once

#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/ui/Panel.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace toki::tools {

/// The checkout's configuration headers as settings, a tab for each header:
/// each setting by its documented name, edited by what it is written as, with
/// the value the build ends with beside it when the safeguards override it.
/// Changes are written once their plan has been reviewed.
class ConfigurePanel : public core::Panel {
public:
    ConfigurePanel(const workspace::WorkspaceService& workspaceService,
                   actor::CollectionCatalog& collectionCatalog);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    [[nodiscard]] bool isTool() const override {
        return true;
    }

    void draw() override;

private:
    void updateSettings();
    void drawGroup(const ConfigGroup& group);
    void drawSetting(const ConfigSetting& setting);
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
    EditFlow editFlow;
};

} // namespace toki::tools
