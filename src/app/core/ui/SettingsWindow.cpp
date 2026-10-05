#include "SettingsWindow.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/SettingsSearch.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr const char* PopupName = "Settings";
constexpr s64 SmallestLog = 100;
constexpr s64 LargestLog = 1'000'000;

template <usize Size>
void copyText(std::array<char, Size>& into, const std::string_view text) {
    const usize length = std::min(text.size(), Size - 1);
    std::copy_n(text.begin(), length, into.begin());
    into[length] = '\0';
}

[[nodiscard]] std::string trimText(const char* text) {
    const std::string_view view(text);
    const usize first = view.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const usize last = view.find_last_not_of(" \t");
    return std::string(view.substr(first, last - first + 1));
}

void drawHelp(const std::string_view help) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, palette::TextMuted);
    ImGui::TextUnformatted(help.data(), help.data() + help.size());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

void drawChangedMark() {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float size = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + (size * 0.3F), at.y + (size * 0.5F)),
                                                size * 0.18F, ImGui::GetColorU32(palette::Warning));
    ImGui::Dummy(ImVec2(size * 0.6F, size));
    ImGui::SetItemTooltip("Changed from its default");
}

} // namespace

SettingsWindow::SettingsWindow() {
    const Settings defaults;
    entries.push_back(Entry{
        .category = "General",
        .label = "HackerOoT folder",
        .help = "The checkout Toki reads and builds. Changing it reads the new one.",
        .drawControl =
            [this] {
                ImGui::TextUnformatted(draft.hackerootPath
                                           ? encodeUtf8(*draft.hackerootPath).c_str()
                                           : "No folder set");
                if (ImGui::Button("Change…")) {
                    changesHackerootPath = true;
                }
            },
        .isChanged = [] { return false; },
        .reset = [] {},
    });
    entries.push_back(Entry{
        .category = "Build",
        .label = "Make's command line",
        .help = "Variables make is run with, such as VERSION or DEBUG_FEATURES. The checkout is "
                "read and built with these; what is left out takes the Makefile's own default.",
        .drawControl = [this] { drawBuildRows(); },
        .isChanged = [this] { return !buildRows.empty(); },
        .reset = [this] { buildRows.clear(); },
    });
    entries.push_back(Entry{
        .category = "Log",
        .label = "Shown from",
        .help = "The least important level the Log panel shows.",
        .drawControl = [this] { drawLogLevel(); },
        .isChanged = [this, defaults] { return draft.log.minLevel != defaults.log.minLevel; },
        .reset = [this, defaults] { draft.log.minLevel = defaults.log.minLevel; },
    });
    entries.push_back(Entry{
        .category = "Log",
        .label = "Entries kept",
        .help = "How many log lines are kept before the oldest are dropped.",
        .drawControl = [this] { drawLogEntries(); },
        .isChanged = [this, defaults] { return draft.log.maxEntries != defaults.log.maxEntries; },
        .reset = [this, defaults] { draft.log.maxEntries = defaults.log.maxEntries; },
    });
}

void SettingsWindow::open(const Settings& current) {
    draft = current;
    buildRows.clear();
    for (const auto& [name, value] : current.build) {
        BuildRow row;
        copyText(row.name, name);
        copyText(row.value, value);
        buildRows.push_back(row);
    }
    search.fill('\0');
    isOpening = true;
}

std::vector<const SettingsWindow::Entry*>
SettingsWindow::findShown(const std::string_view inCategory) const {
    std::vector<const Entry*> shown;
    const std::string_view query(search.data());
    for (const Entry& entry : entries) {
        const std::array<std::string_view, 3> texts{entry.label, entry.help, entry.category};
        if ((inCategory.empty() || entry.category == inCategory) && matchesSearch(texts, query)) {
            shown.push_back(&entry);
        }
    }
    return shown;
}

std::vector<std::string_view> SettingsWindow::findCategories() const {
    std::vector<std::string_view> categories;
    for (const Entry* entry : findShown({})) {
        if (std::ranges::find(categories, entry->category) == categories.end()) {
            categories.push_back(entry->category);
        }
    }
    return categories;
}

SettingsActions SettingsWindow::draw() {
    SettingsActions actions;
    if (isOpening) {
        ImGui::OpenPopup(PopupName);
        isOpening = false;
    }
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(em * 46.0F, em * 30.0F), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return actions;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##search", "Search settings", search.data(), search.size());

    const std::vector<std::string_view> categories = findCategories();
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("##categories", ImVec2(em * 10.0F, -footer), ImGuiChildFlags_Borders)) {
        drawCategories(categories);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##page", ImVec2(0.0F, -footer))) {
        drawPage(categories);
    }
    ImGui::EndChild();

    if (changesHackerootPath) {
        changesHackerootPath = false;
        actions.changeHackerootPath = true;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Apply")) {
        actions.applied = makeApplied();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::Shortcut(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return actions;
}

void SettingsWindow::drawCategories(const std::vector<std::string_view>& categories) {
    if (!categories.empty() && std::ranges::find(categories, category) == categories.end()) {
        category = categories.front();
    }
    for (const std::string_view name : categories) {
        const usize count = findShown(name).size();
        const std::string label = std::format("{}###{}", name, name);
        if (ImGui::Selectable(label.c_str(), name == category)) {
            category = name;
        }
        const std::string counted = std::to_string(count);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(counted.c_str()).x);
        ImGui::TextColored(palette::TextMuted, "%s", counted.c_str());
    }
    if (categories.empty()) {
        ImGui::TextColored(palette::TextMuted, "No match");
    }
}

void SettingsWindow::drawPage(const std::vector<std::string_view>& categories) {
    const bool isSearching = search[0] != '\0';
    if (isSearching) {
        for (const Entry* entry : findShown({})) {
            drawEntry(*entry, true);
        }
        if (categories.empty()) {
            ImGui::TextColored(palette::TextMuted, "No setting matches \"%s\".", search.data());
        }
        return;
    }
    ImGui::SeparatorText(std::string(category).c_str());
    for (const Entry* entry : findShown(category)) {
        drawEntry(*entry, false);
    }
}

void SettingsWindow::drawEntry(const Entry& entry, const bool showsCategory) {
    ImGui::PushID(entry.label.data(), entry.label.data() + entry.label.size());
    const bool changed = entry.isChanged();
    if (changed) {
        drawChangedMark();
        ImGui::SameLine(0.0F, 0.0F);
    }
    if (showsCategory) {
        ImGui::TextColored(palette::TextMuted, "%.*s ›", static_cast<int>(entry.category.size()),
                           entry.category.data());
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(entry.label.data(), entry.label.data() + entry.label.size());
    if (changed) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            entry.reset();
        }
    }
    drawHelp(entry.help);
    entry.drawControl();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PopID();
}

void SettingsWindow::drawBuildRows() {
    std::optional<usize> removed;
    constexpr ImGuiTableFlags Flags = ImGuiTableFlags_SizingStretchProp;
    if (!buildRows.empty() && ImGui::BeginTable("##build", 3, Flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
        for (usize i = 0; i < buildRows.size(); ++i) {
            BuildRow& row = buildRows[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##name", "NAME", row.name.data(), row.name.size());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##value", "value", row.value.data(), row.value.size());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(icons::X)) {
                removed = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removed) {
        buildRows.erase(buildRows.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
    if (ImGui::Button("Add variable")) {
        buildRows.emplace_back();
    }
}

void SettingsWindow::drawLogLevel() {
    const std::string current(getLogLevelName(draft.log.minLevel));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
    if (ImGui::BeginCombo("##level", current.c_str())) {
        for (usize i = 0; i < LogLevelCount; ++i) {
            const auto level = static_cast<LogLevel>(i);
            const std::string name(getLogLevelName(level));
            if (ImGui::Selectable(name.c_str(), level == draft.log.minLevel)) {
                draft.log.minLevel = level;
            }
        }
        ImGui::EndCombo();
    }
}

void SettingsWindow::drawLogEntries() {
    auto kept = static_cast<s64>(draft.log.maxEntries);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
    if (ImGui::InputScalar("##entries", ImGuiDataType_S64, &kept)) {
        draft.log.maxEntries = static_cast<usize>(std::clamp(kept, SmallestLog, LargestLog));
    }
}

Settings SettingsWindow::makeApplied() const {
    Settings applied = draft;
    applied.build.clear();
    for (const BuildRow& row : buildRows) {
        std::string name = trimText(row.name.data());
        if (!name.empty()) {
            applied.build.insert_or_assign(std::move(name), trimText(row.value.data()));
        }
    }
    return applied;
}

} // namespace toki::core
