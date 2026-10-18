#pragma once

#include "app/core/Types.h"
#include "app/core/domain/Settings.h"

#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace toki::core {

/// What was done in the settings window this frame.
struct SettingsActions {
    /// The settings as edited, when they were applied.
    std::optional<Settings> applied;
    /// The HackerOoT folder is to be chosen again, which the setup wizard does.
    bool changeHackerootPath = false;
};

/// A modal window over the settings that have no other place: a list of
/// categories beside the page of the one chosen, and a search across all of
/// them. Each setting is one row, its help the tooltip of its label; one that
/// differs from its default has a button that resets it.
/// It edits a copy, and hands it back only when applied.
///
/// Every setting is one entry: adding one is adding an entry, which search,
/// the category counts and the marks then cover.
class SettingsWindow {
public:
    SettingsWindow();
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;
    SettingsWindow(SettingsWindow&&) = delete;
    SettingsWindow& operator=(SettingsWindow&&) = delete;
    ~SettingsWindow() = default;

    void open(const Settings& current);

    [[nodiscard]] SettingsActions draw();

private:
    struct Entry {
        std::string_view category;
        std::string_view label;
        std::string_view help;
        std::function<void()> drawControl;
        std::function<bool()> isChanged;
        std::function<void()> reset;
    };

    [[nodiscard]] std::vector<const Entry*> findShown(std::string_view category) const;
    [[nodiscard]] std::vector<std::string_view> findCategories() const;
    void drawCategories(const std::vector<std::string_view>& categories);
    void drawPage(const std::vector<std::string_view>& categories);
    static void drawEntry(const Entry& entry, bool showsCategory);
    void drawHackerootPath();
    void drawMakeVariables();
    void drawLogLevel();
    void drawLogEntries();
    void drawMarkerShape();
    void drawSceneSaveReview();
    void drawAngleFormat();
    [[nodiscard]] Settings makeApplied() const;

    std::vector<Entry> entries;
    Settings draft;
    std::array<char, 1024> buildLine{};
    std::array<char, 128> search{};
    std::string_view category = "General";
    bool isOpening = false;
    bool changesHackerootPath = false;
};

} // namespace toki::core
