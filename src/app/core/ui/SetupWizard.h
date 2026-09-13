#pragma once

#include "app/core/Types.h"
#include "app/core/ui/FolderDialog.h"
#include "app/core/ui/Fonts.h"

#include <array>
#include <filesystem>
#include <optional>

struct SDL_Window;

namespace toki::core {

class SetupWizard {
public:
    explicit SetupWizard(const Fonts& fonts) : fonts(fonts) {}

    SetupWizard(const SetupWizard&) = delete;
    SetupWizard& operator=(const SetupWizard&) = delete;
    SetupWizard(SetupWizard&&) = delete;
    SetupWizard& operator=(SetupWizard&&) = delete;
    ~SetupWizard() = default;

    void beginFirstRun();

    void beginChangePath(const std::optional<std::filesystem::path>& current);

    [[nodiscard]] bool isActive() const {
        return active;
    }

    [[nodiscard]] std::optional<std::filesystem::path> draw(SDL_Window* parent);

private:
    enum class Step : u8 {
        Welcome,
        ChoosePath,
    };

    void drawWelcome();
    [[nodiscard]] std::optional<std::filesystem::path> drawPathStep(SDL_Window* parent);

    const Fonts& fonts;
    FolderDialog folderDialog;
    Step step = Step::Welcome;
    bool active = false;
    bool cancellable = false;
    std::array<char, 512> pathInput{};
};

} // namespace toki::core
