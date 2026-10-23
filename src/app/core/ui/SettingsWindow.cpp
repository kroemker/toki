#include "SettingsWindow.h"

#include "app/core/Types.h"
#include "app/core/domain/BuildChoices.h"
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
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr const char* PopupName = "Settings";
constexpr s64 SmallestLog = 100;
constexpr s64 LargestLog = 1'000'000;
constexpr float LabelWidth = 11.0F;
constexpr auto MarkerShapeLabels = std::to_array<const char*>({"Cube", "Diamond", "Pin"});
constexpr auto AngleFormatLabels = std::to_array<const char*>({"Binary Angles", "Degrees"});

static_assert(AngleFormatLabels.size() == AngleFormatCount,
              "every angle format needs a label in the settings window");
constexpr auto LogLevelLabels =
    std::to_array<const char*>({"Debug", "Info", "OK", "Warning", "Error"});

constexpr std::string_view VersionVariable = "VERSION";
constexpr std::string_view DebugFeaturesVariable = "DEBUG_FEATURES";
constexpr std::array OwnedVariables = {VersionVariable, DebugFeaturesVariable};

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

[[nodiscard]] bool isOwnedVariable(const std::string_view name) {
    return std::ranges::find(OwnedVariables, name) != OwnedVariables.end();
}

[[nodiscard]] std::optional<std::string>
findVariable(const std::map<std::string, std::string, std::less<>>& variables,
             const std::string_view name) {
    const auto found = variables.find(name);
    return found == variables.end() ? std::nullopt : std::optional(found->second);
}

void eraseVariable(std::map<std::string, std::string, std::less<>>& variables,
                   const std::string_view name) {
    if (const auto found = variables.find(name); found != variables.end()) {
        variables.erase(found);
    }
}

[[nodiscard]] std::string encodeNative(std::filesystem::path path) {
    const std::u8string encoded = path.make_preferred().u8string();
    return {encoded.begin(), encoded.end()};
}

[[nodiscard]] std::string_view trimPath(const std::string_view text) {
    constexpr std::string_view Padding = " \t\"";
    const usize first = text.find_first_not_of(Padding);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(Padding) - first + 1);
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
        .category = "General",
        .label = "Emulator",
        .help = "The program Run opens the built ROM in. Toki starts it itself and hands it the "
                "ROM's Windows path, \\\\wsl.localhost\\... for a checkout in WSL.",
        .drawControl = [this] { drawEmulator(); },
        .isChanged = [this] { return emulatorLine[0] != '\0'; },
        .reset = [this] { emulatorLine.fill('\0'); },
    });
    entries.push_back(Entry{
        .category = "Build",
        .label = "Version",
        .help = "The game version the checkout is read and built as, VERSION. The versions "
                "make setup extracted into the checkout are offered. Default is the Makefile's.",
        .drawControl = [this] { drawVersion(); },
        .isChanged = [this] { return draft.build.contains(VersionVariable); },
        .reset = [this] { eraseVariable(draft.build, VersionVariable); },
    });
    entries.push_back(Entry{
        .category = "Build",
        .label = "Debug Features",
        .help = "Whether the checkout is read and built with the debug features, "
                "DEBUG_FEATURES. Default is what the Makefile gives the version.",
        .drawControl = [this] { drawDebugFeatures(); },
        .isChanged = [this] { return draft.build.contains(DebugFeaturesVariable); },
        .reset = [this] { eraseVariable(draft.build, DebugFeaturesVariable); },
    });
    entries.push_back(Entry{
        .category = "Build",
        .label = "Other Variables",
        .help = "Further variables make is run with, as on its command line: COMPILER=ido "
                "REGION=EU. The checkout is read and built with these; what is left out takes "
                "the Makefile's own default. Quote a value that holds a space.",
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
        .category = "Scenes",
        .label = "Review Saves",
        .help = "Shows what saving a scene writes, and waits for Write Files. Off, a save is "
                "written at once, and what it warns of goes to the log.",
        .drawControl = [this] { drawSceneSaveReview(); },
        .isChanged = [this,
                      defaults] { return draft.reviewsSceneSaves != defaults.reviewsSceneSaves; },
        .reset = [this, defaults] { draft.reviewsSceneSaves = defaults.reviewsSceneSaves; },
    });
    entries.push_back(Entry{
        .category = "Scenes",
        .label = "Rotations In",
        .help = "How a placement's rotation is shown and typed: as the binary angles the game "
                "stores, 0x4000 a quarter turn, or in degrees. The button beside a rotation in "
                "the Properties panel switches it too.",
        .drawControl = [this] { drawAngleFormat(); },
        .isChanged = [this, defaults] { return draft.angleFormat != defaults.angleFormat; },
        .reset = [this, defaults] { draft.angleFormat = defaults.angleFormat; },
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

void SettingsWindow::open(const Settings& current, BuildChoices choices) {
    draft = current;
    buildChoices = std::move(choices);
    openedVersion = findVariable(current.build, VersionVariable);
    std::map<std::string, std::string, std::less<>> others = current.build;
    std::erase_if(others, [](const auto& variable) { return isOwnedVariable(variable.first); });
    std::erase_if(draft.build,
                  [](const auto& variable) { return !isOwnedVariable(variable.first); });
    copyText(buildLine, formatMakeVariables(others));
    copyText(emulatorLine, current.emulatorPath ? encodeNative(*current.emulatorPath) : "");
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

SettingsActions SettingsWindow::draw(SDL_Window* parent) {
    SettingsActions actions;
    dialogParent = parent;
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

void SettingsWindow::drawVersion() {
    std::vector<VariableOption> options;
    for (const std::string& version : buildChoices.versions) {
        options.push_back(VariableOption{.label = version, .value = version});
    }
    if (const std::optional<std::string> set = findVariable(draft.build, VersionVariable);
        set && std::ranges::find(options, *set, &VariableOption::value) == options.end()) {
        options.push_back(
            VariableOption{.label = std::format("{} (not extracted)", *set), .value = *set});
    }
    drawVariableCombo(VersionVariable, options);
}

void SettingsWindow::drawDebugFeatures() {
    const std::array options{VariableOption{.label = "On", .value = "1"},
                             VariableOption{.label = "Off", .value = "0"}};
    drawVariableCombo(DebugFeaturesVariable, options);
}

void SettingsWindow::drawVariableCombo(const std::string_view name,
                                       const std::span<const VariableOption> options) {
    const std::optional<std::string> set = findVariable(draft.build, name);
    const std::string defaultLabel = makeDefaultLabel(name, options);
    std::string preview = defaultLabel;
    if (set) {
        const auto found = std::ranges::find(options, *set, &VariableOption::value);
        preview = found == options.end() ? *set : found->label;
    }
    if (!ImGui::BeginCombo("##variable", preview.c_str())) {
        return;
    }
    if (ImGui::Selectable(defaultLabel.c_str(), !set)) {
        eraseVariable(draft.build, name);
    }
    for (const VariableOption& option : options) {
        if (ImGui::Selectable(option.label.c_str(), set == option.value)) {
            draft.build.insert_or_assign(std::string(name), option.value);
        }
    }
    ImGui::EndCombo();
}

std::string SettingsWindow::makeDefaultLabel(const std::string_view name,
                                             const std::span<const VariableOption> options) const {
    if (name != VersionVariable && findVariable(draft.build, VersionVariable) != openedVersion) {
        return "Default";
    }
    const std::optional<std::string> value = findVariable(buildChoices.defaults, name);
    if (!value) {
        return "Default";
    }
    const auto found = std::ranges::find(options, *value, &VariableOption::value);
    return std::format("Default ({})", found == options.end() ? *value : found->label);
}

void SettingsWindow::drawMakeVariables() {
    const MakeVariables read = parseMakeVariables(buildLine.data());
    std::string owned;
    for (const auto& [name, value] : read.variables) {
        if (isOwnedVariable(name)) {
            owned += owned.empty() ? name : " " + name;
        }
    }
    std::string rejected;
    for (const std::string& word : read.rejected) {
        rejected += rejected.empty() ? word : " " + word;
    }
    const bool leavesOut = !owned.empty() || !rejected.empty();
    if (leavesOut) {
        ImGui::PushStyleColor(ImGuiCol_Border, palette::Danger);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    }
    ImGui::InputTextWithHint("##make", "COMPILER=ido REGION=EU", buildLine.data(),
                             buildLine.size());
    if (!leavesOut) {
        return;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    std::string tooltip;
    if (!rejected.empty()) {
        tooltip = std::format("Not NAME=value, left out: {}", rejected);
    }
    if (!owned.empty()) {
        tooltip += std::format("{}Set in a row of its own, left out: {}",
                               tooltip.empty() ? "" : "\n", owned);
    }
    ImGui::SetItemTooltip("%s", tooltip.c_str());
}

void SettingsWindow::drawEmulator() {
    if (const std::optional<std::filesystem::path> chosen = pathDialog.takeChoice()) {
        copyText(emulatorLine, encodeNative(*chosen));
    }
    const char* browse = "Browse…";
    const float buttonWidth =
        ImGui::CalcTextSize(browse).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() -
                            (buttonWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##emulator", "No emulator set", emulatorLine.data(),
                             emulatorLine.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(pathDialog.isPending());
    if (ImGui::Button(browse)) {
        const std::string_view current = trimPath(emulatorLine.data());
        pathDialog.openProgram(dialogParent, current.empty() ? std::filesystem::path{}
                                                             : decodeUtf8(current).parent_path());
    }
    ImGui::EndDisabled();
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

void SettingsWindow::drawAngleFormat() {
    auto picked = static_cast<usize>(draft.angleFormat);
    if (drawLabelCombo("##angleFormat", AngleFormatLabels, picked)) {
        draft.angleFormat = static_cast<AngleFormat>(picked);
    }
}

void SettingsWindow::drawSceneSaveReview() {
    ImGui::Checkbox("##reviewsSceneSaves", &draft.reviewsSceneSaves);
}

Settings SettingsWindow::makeApplied() const {
    Settings applied = draft;
    for (auto& [name, value] : parseMakeVariables(buildLine.data()).variables) {
        if (!isOwnedVariable(name)) {
            applied.build.insert_or_assign(name, std::move(value));
        }
    }
    const std::string_view emulator = trimPath(emulatorLine.data());
    applied.emulatorPath = emulator.empty() ? std::nullopt : std::optional(decodeUtf8(emulator));
    return applied;
}

} // namespace toki::core
