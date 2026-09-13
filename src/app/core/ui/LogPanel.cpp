#include "LogPanel.h"

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace toki::core {

namespace {

struct LevelStyle {
    LogLevel level = LogLevel::Info;
    const char* tag = "";
    ImVec4 color;
};

constexpr std::array<LevelStyle, LogLevelCount> Levels = {{
    {LogLevel::Debug, "DEBUG", palette::TextMuted},
    {LogLevel::Info, "INFO ", palette::Text},
    {LogLevel::Ok, "OK   ", palette::Success},
    {LogLevel::Warn, "WARN ", palette::Warning},
    {LogLevel::Error, "ERROR", palette::Danger},
}};

consteval bool levelsAreComplete() {
    for (usize index = 0; index < Levels.size(); ++index) {
        if (static_cast<usize>(std::to_underlying(Levels[index].level)) != index) {
            return false;
        }
    }
    return true;
}

static_assert(levelsAreComplete(), "Levels must hold one style per LogLevel, in enum order");

[[nodiscard]] const LevelStyle& styleOf(LogLevel level) {
    return Levels.at(static_cast<usize>(std::to_underlying(level)));
}

[[nodiscard]] char lowered(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool containsIgnoringCase(std::string_view haystack, std::string_view needle) {
    const auto found = std::ranges::search(haystack, needle,
                                           [](char a, char b) { return lowered(a) == lowered(b); });
    return !found.empty();
}

[[nodiscard]] std::string formatEntry(const LogEntry& entry) {
    return std::format("[{}] {}", styleOf(entry.level).tag, entry.message);
}

} // namespace

LogPanel::LogPanel(Log& log, LogSettings& logSettings, const Fonts& fonts)
    : Panel("log", "Log", icons::Scroll, DockSlot::Bottom), log(log), logSettings(logSettings),
      fonts(fonts) {}

void LogPanel::collectVisible() {
    const std::string_view needle{filter.data()};
    const LogLevel minLevel = logSettings.minLevel;

    visible.clear();
    log.forEach([&](const usize index, const LogEntry& entry) {
        if (entry.level < minLevel) {
            return;
        }
        if (!needle.empty() && !containsIgnoringCase(entry.message, needle)) {
            return;
        }
        visible.push_back(index);
    });
}

void LogPanel::copyVisible() const {
    std::string text;
    for (const usize index : visible) {
        if (const std::optional<LogEntry> entry = log.entry(index)) {
            text += formatEntry(*entry);
            text += '\n';
        }
    }
    ImGui::SetClipboardText(text.c_str());
}

void LogPanel::drawToolbar() {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
    if (ImGui::BeginCombo("##level", styleOf(logSettings.minLevel).tag)) {
        for (const LevelStyle& level : Levels) {
            ImGui::PushStyleColor(ImGuiCol_Text, level.color);
            if (ImGui::Selectable(level.tag, level.level == logSettings.minLevel)) {
                logSettings.minLevel = level.level;
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Minimum level to show");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
    ImGui::InputTextWithHint("##filter", "Filter…", filter.data(), filter.size());

    ImGui::SameLine();
    if (ImGui::SmallButton(icons::X)) {
        filter.fill('\0');
    }
    ImGui::SetItemTooltip("Clear the filter");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::GetFontSize() + (style.FramePadding.x * 2.0F);
    const float buttonsWidth = (buttonWidth * 2.0F) + style.ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttonsWidth);
    if (ImGui::Button(icons::Copy)) {
        copyVisible();
    }
    ImGui::SetItemTooltip("Copy the visible lines");

    ImGui::SameLine();
    if (ImGui::Button(icons::Broom)) {
        log.clear();
    }
    ImGui::SetItemTooltip("Clear the log");
}

void LogPanel::drawEntries() {
    if (!ImGui::BeginChild("entries", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    if (visible.empty()) {
        ImGui::TextColored(palette::TextMuted,
                           log.size() == 0 ? "No messages." : "Nothing matches the filter.");
        ImGui::EndChild();
        return;
    }

    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();

    ImGui::PushFont(fonts.getMono(), Fonts::MonoSize);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::optional<LogEntry> entry = log.entry(visible[static_cast<usize>(row)]);
            if (!entry) {
                continue;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, styleOf(entry->level).color);
            ImGui::TextUnformatted(formatEntry(*entry).c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopFont();

    if (atBottom) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();
}

void LogPanel::draw() {
    collectVisible();
    drawToolbar();
    ImGui::Separator();
    drawEntries();
}

} // namespace toki::core
