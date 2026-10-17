#include "SettingsWindow.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/MakeVariables.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/SettingsSearch.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

namespace {

constexpr const char* PopupName = "Settings";
constexpr s64 SmallestLog = 100;
constexpr s64 LargestLog = 1'000'000;
constexpr float LabelWidth = 11.0F;
constexpr auto MarkerShapeLabels = std::to_array<const char*>({"Cube", "Diamond", "Pin"});
constexpr auto LogLevelLabels =
    std::to_array<const char*>({"Debug", "Info", "OK", "Warning", "Error"});

static_assert(MarkerShapeLabels.size() == MarkerShapeCount,
              "a marker shape was added to the enum without a label");
static_assert(LogLevelLabels.size() == LogLevelCount,
              "a log level was added to the enum without a label");

template <usize Size>
void copyText(std::array<char, Size>& into, const std::string_view text) {
    const usize length = std::min(text.size(), Size - 1);
    std::copy_n(text.begin(), length, into.begin());
    into[length] = '\0';
}

template <usize Count>
[[nodiscard]] bool drawLabelCombo(const char* id, const std::array<const char*, Count>& labels,
                                  usize& picked) {
    bool isPicked = false;
    if (ImGui::BeginCombo(id, labels[picked])) {
        for (usize i = 0; i < labels.size(); ++i) {
            if (ImGui::Selectable(labels[i], i == picked)) {
                picked = i;
                isPicked = true;
            }
        }
        ImGui::EndCombo();
    }
    return isPicked;
}

} // namespace

SettingsWindow::SettingsWindow() {
    const Settings defaults;
    entries.push_back(Entry{
        .category = "General",
        .label = "HackerOoT Folder",
        .help = "The checkout Toki reads and builds. Changing it reads the new one.",
        .drawControl = [this] { drawHackerootPath(); },
        .isChanged = [] { return false; },
        .reset = [] {},
    });
    entries.push_back(Entry{
        .category = "Build",
        .label = "Make Variables",
        .help = "Variables make is run with, as on its command line: VERSION=gc-eu-mq "
                "DEBUG_FEATURES=1. The checkout is read and built with these; what is left out "
                "takes the Makefile's own default. Quote a value that holds a space.",
        .drawControl = [this] { drawMakeVariables(); },
        .isChanged = [this] { return buildLine[0] != '\0'; },
        .reset = [this] { buildLine.fill('\0'); },
    });
    entries.push_back(Entry{
        .category = "Viewport",
        .label = "Actor Markers",
        .help = "The shape an actor is marked with when it is not drawn as its model. Player "
                "entries are always marked.",
        .drawControl = [this] { drawMarkerShape(); },
        .isChanged = [this, defaults] { return draft.markerShape != defaults.markerShape; },
        .reset = [this, defaults] { draft.markerShape = defaults.markerShape; },
    });
    entries.push_back(Entry{
        .category = "Log",
        .label = "Min. Log Level",
        .help = "The least important level the Log panel shows.",
        .drawControl = [this] { drawLogLevel(); },
        .isChanged = [this, defaults] { return draft.log.minLevel != defaults.log.minLevel; },
        .reset = [this, defaults] { draft.log.minLevel = defaults.log.minLevel; },
    });
    entries.push_back(Entry{
        .category = "Log",
        .label = "Max Entries",
        .help = "How many log lines are kept before the oldest are dropped.",
        .drawControl = [this] { drawLogEntries(); },
        .isChanged = [this, defaults] { return draft.log.maxEntries != defaults.log.maxEntries; },
        .reset = [this, defaults] { draft.log.maxEntries = defaults.log.maxEntries; },
    });
}

void SettingsWindow::open(const Settings& current) {
    draft = current;
    copyText(buildLine, formatMakeVariables(current.build));
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
    if (!isSearching) {
        ImGui::SeparatorText(std::string(category).c_str());
    }
    if (beginProperties("##settings", LabelWidth)) {
        for (const Entry* entry : findShown(isSearching ? std::string_view{} : category)) {
            drawEntry(*entry, isSearching);
        }
        endProperties();
    }
    if (isSearching && categories.empty()) {
        ImGui::TextColored(palette::TextMuted, "No setting matches \"%s\".", search.data());
    }
}

void SettingsWindow::drawEntry(const Entry& entry, const bool showsCategory) {
    ImGui::PushID(entry.label.data(), entry.label.data() + entry.label.size());
    const std::string label = showsCategory ? std::format("{} › {}", entry.category, entry.label)
                                            : std::string(entry.label);
    drawPropertyLabel(label, entry.help);
    const bool changed = entry.isChanged();
    const float resetWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(changed ? -resetWidth : -FLT_MIN);
    entry.drawControl();
    if (changed) {
        ImGui::SameLine();
        if (ImGui::Button(icons::ArrowCounterClockwise,
                          ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
            entry.reset();
        }
        ImGui::SetItemTooltip("Reset to its default");
    }
    ImGui::PopID();
}

void SettingsWindow::drawHackerootPath() {
    std::string path = draft.hackerootPath ? encodeUtf8(*draft.hackerootPath) : "No folder set";
    const char* change = "Change…";
    const float buttonWidth =
        ImGui::CalcTextSize(change).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SetNextItemWidth(-(buttonWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##path", path.data(), path.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button(change)) {
        changesHackerootPath = true;
    }
}

void SettingsWindow::drawMakeVariables() {
    const MakeVariables read = parseMakeVariables(buildLine.data());
    const bool hasRejected = !read.rejected.empty();
    if (hasRejected) {
        ImGui::PushStyleColor(ImGuiCol_Border, palette::Danger);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    }
    ImGui::InputTextWithHint("##make", "VERSION=gc-eu-mq DEBUG_FEATURES=1", buildLine.data(),
                             buildLine.size());
    if (hasRejected) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        std::string words;
        for (const std::string& word : read.rejected) {
            words += words.empty() ? word : " " + word;
        }
        ImGui::SetItemTooltip("Not NAME=value, left out: %s", words.c_str());
    }
}

void SettingsWindow::drawLogLevel() {
    auto picked = static_cast<usize>(draft.log.minLevel);
    if (drawLabelCombo("##level", LogLevelLabels, picked)) {
        draft.log.minLevel = static_cast<LogLevel>(picked);
    }
}

void SettingsWindow::drawLogEntries() {
    auto kept = static_cast<s64>(draft.log.maxEntries);
    if (ImGui::InputScalar("##entries", ImGuiDataType_S64, &kept)) {
        draft.log.maxEntries = static_cast<usize>(std::clamp(kept, SmallestLog, LargestLog));
    }
}

void SettingsWindow::drawMarkerShape() {
    auto picked = static_cast<usize>(draft.markerShape);
    if (drawLabelCombo("##markerShape", MarkerShapeLabels, picked)) {
        draft.markerShape = static_cast<MarkerShape>(picked);
    }
}

Settings SettingsWindow::makeApplied() const {
    Settings applied = draft;
    applied.build = parseMakeVariables(buildLine.data()).variables;
    return applied;
}

} // namespace toki::core
