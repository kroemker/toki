#pragma once

#include "app/core/Types.h"
#include "app/core/domain/BuildChoices.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/PathDialog.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

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

    /// Opens on `current`, offering what `choices` says the checkout read
    /// under it has.
    void open(const Settings& current, BuildChoices choices);

    /// `parent` is the window the emulator's file dialog belongs to.
    [[nodiscard]] SettingsActions draw(SDL_Window* parent);

private:
    struct Entry {
        std::string_view category;
        std::string_view label;
        std::string_view help;
        std::function<void()> drawControl;
        std::function<bool()> isChanged;
        std::function<void()> reset;
    };

    /// One option of a make variable's dropdown: its label and the value it
    /// sets.
    struct VariableOption {
        std::string label;
        std::string value;
    };

    [[nodiscard]] std::vector<const Entry*> findShown(std::string_view category) const;
    [[nodiscard]] std::vector<std::string_view> findCategories() const;
    void drawCategories(const std::vector<std::string_view>& categories);
    void drawPage(const std::vector<std::string_view>& categories);
    static void drawEntry(const Entry& entry, bool showsCategory);
    void drawHackerootPath();
    void drawVersion();
    void drawDebugFeatures();
    void drawVariableCombo(std::string_view name, std::span<const VariableOption> options);
    [[nodiscard]] std::string makeDefaultLabel(std::string_view name,
                                               std::span<const VariableOption> options) const;
    void drawMakeVariables();
    void drawEmulator();
    void drawLogLevel();
    void drawLogEntries();
    void drawMarkerShape();
    void drawSceneSaveReview();
    void drawAngleFormat();
    [[nodiscard]] Settings makeApplied() const;

    std::vector<Entry> entries;
    Settings draft;
    BuildChoices buildChoices;
    /// The `VERSION` the settings had when opened, which `buildChoices` was
    /// read under: the defaults it gives hold only while the draft keeps it.
    std::optional<std::string> openedVersion;
    std::array<char, 1024> buildLine{};
    std::array<char, 1024> emulatorLine{};
    PathDialog pathDialog;
    SDL_Window* dialogParent = nullptr;
    std::array<char, 128> search{};
    std::string_view category = "General";
    bool isOpening = false;
    bool changesHackerootPath = false;
};

} // namespace toki::core
