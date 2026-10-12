#include "ConfigureDialog.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace toki::configure {

namespace {

constexpr float LabelWidth = 16.0F;
constexpr float NoteWidth = 12.0F;
constexpr float ChoiceListHeight = 18.0F;

[[nodiscard]] usize countChanges(const std::vector<ConfigGroup>& groups,
                                 const std::map<std::string, std::string, std::less<>>& values) {
    usize changes = 0;
    for (const ConfigGroup& group : groups) {
        for (const ConfigSetting& setting : group.settings) {
            const auto value = values.find(setting.define.name);
            changes += value != values.end() && value->second != setting.define.written ? 1 : 0;
        }
    }
    return changes;
}

} // namespace

ConfigureDialog::ConfigureDialog(const workspace::WorkspaceService& workspaceService,
                                 actor::CollectionCatalog& collectionCatalog)
    : Dialog("configure", "Configure", core::icons::Gear, core::DialogMode::Floating, 50.0F, 34.0F),
      workspaceService(workspaceService), collectionCatalog(collectionCatalog) {}

void ConfigureDialog::updateSettings() {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (workspace != listedWorkspace) {
        isStale = true;
    }
    if (isStale && !reading.valid()) {
        listedWorkspace = workspace;
        isStale = false;
        if (workspace != nullptr) {
            readingRoot = workspace->getRoot();
            reading = std::async(
                std::launch::async,
                [root = workspace->getRoot(), arguments = workspace->getBuildDefines().arguments] {
                    return hackeroot::readConfigDefines(root, arguments);
                });
        }
    }
    if (reading.valid() && reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        groupsRoot = readingRoot;
        groups = makeConfigGroups(reading.get());
        std::erase_if(values, [this](const auto& value) {
            return std::ranges::none_of(groups, [&value](const ConfigGroup& group) {
                return std::ranges::any_of(group.settings, [&value](const ConfigSetting& setting) {
                    return setting.define.name == value.first &&
                           setting.define.written != value.second;
                });
            });
        });
    }
}

void ConfigureDialog::draw() {
    updateSettings();
    if (listedWorkspace == nullptr) {
        ImGui::TextColored(core::palette::TextMuted, "Open a checkout to configure it.");
        return;
    }
    if (groups.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "Reading the configuration…");
        return;
    }
    ImGui::TextColored(core::palette::TextMuted,
                       "The settings of include/config/, as the build reads them.");
    const usize changes = countChanges(groups, values);
    ImGui::BeginDisabled(changes == 0 || editFlow.isBusy() || editFlow.isReviewing());
    const std::string review =
        std::format("Review {} {}", changes, changes == 1 ? "change" : "changes");
    if (ImGui::Button(review.c_str())) {
        editFlow.requestPlan(groupsRoot, [root = groupsRoot, groups = groups, values = values] {
            return makeConfigPlan(root, groups, values);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        values.clear();
    }
    ImGui::EndDisabled();
    if (editFlow.draw(groupsRoot)) {
        isStale = true;
    }
    if (!ImGui::BeginTabBar("##groups")) {
        return;
    }
    const bool isLocked = editFlow.isReviewing() || editFlow.isBusy();
    for (const ConfigGroup& group : groups) {
        if (ImGui::BeginTabItem(group.title.c_str())) {
            ImGui::BeginDisabled(isLocked);
            drawGroup(group);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

void ConfigureDialog::drawGroup(const ConfigGroup& group) {
    if (!ImGui::BeginTable("##settings", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * LabelWidth);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * NoteWidth);
    for (const ConfigSetting& setting : group.settings) {
        ImGui::PushID(setting.define.name.c_str());
        drawSetting(setting);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ConfigureDialog::drawSetting(const ConfigSetting& setting) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    const auto edited = values.find(setting.define.name);
    const bool isChanged = edited != values.end() && edited->second != setting.define.written;
    ImGui::TextColored(isChanged ? core::palette::Accent : core::palette::Text, "%s",
                       setting.label.c_str());
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
        if (!setting.define.comment.empty()) {
            ImGui::TextUnformatted(setting.define.comment.c_str());
        }
        ImGui::TextColored(core::palette::TextMuted, "%s  ·  %s:%zu", setting.define.name.c_str(),
                           setting.define.file.generic_string().c_str(), setting.define.line);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::TableNextColumn();
    std::string value = isChanged ? edited->second : setting.define.written;
    if (drawControl(setting, value)) {
        values[setting.define.name] = value;
    }
    ImGui::TableNextColumn();
    if (isOverridden(setting.define)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(core::palette::Warning, "%s build: %s", core::icons::Warning,
                           setting.define.effective.value_or("undefined").c_str());
        ImGui::SetItemTooltip("config_safeguards.h overrides what is written here");
    } else if (isChanged) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(core::palette::TextMuted, "was %s", setting.define.written.c_str());
    }
}

bool ConfigureDialog::drawControl(const ConfigSetting& setting, std::string& value) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (setting.control) {
    case SettingControl::Toggle: {
        bool isOn = value == "true";
        if (ImGui::Checkbox("##value", &isOn)) {
            value = isOn ? "true" : "false";
            return true;
        }
        return false;
    }
    case SettingControl::Choice:
        return drawChoice(setting, value);
    case SettingControl::Integer:
    case SettingControl::Decimal:
    case SettingControl::Text: {
        const bool isChanged = ImGui::InputText("##value", &value);
        if (!isValidValue(setting, value)) {
            ImGui::SetItemTooltip("Not a value it can be: %s",
                                  setting.control == SettingControl::Text ? "it is empty"
                                                                          : "a number is wanted");
        }
        return isChanged && isValidValue(setting, value);
    }
    }
    return false;
}

bool ConfigureDialog::drawChoice(const ConfigSetting& setting, std::string& value) {
    std::vector<std::string> options = setting.options;
    if (!setting.collection.empty()) {
        if (const actor::ValueCollection* collection =
                collectionCatalog.findSharedCollection(setting.collection)) {
            for (const actor::ChoiceValue& choice : collection->values) {
                options.push_back(choice.label);
            }
        }
    }
    if (!ImGui::BeginCombo("##value", value.c_str(), ImGuiComboFlags_HeightLargest)) {
        return false;
    }
    if (ImGui::IsWindowAppearing()) {
        choiceFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    bool isChanged = false;
    if (options.size() > setting.options.size() || options.size() > 12) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter", &choiceFilter);
    }
    if (ImGui::BeginChild("##options", ImVec2(0.0F, std::min(static_cast<float>(options.size()),
                                                             ChoiceListHeight) *
                                                        ImGui::GetFrameHeightWithSpacing()))) {
        for (const std::string& option : options) {
            if (!core::containsIgnoringCase(option, choiceFilter)) {
                continue;
            }
            if (ImGui::Selectable(option.c_str(), option == value)) {
                isChanged = option != value;
                value = option;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndCombo();
    return isChanged;
}

} // namespace toki::configure
