#pragma once

#include "app/core/Types.h"

#include <string>

namespace toki::core {

enum class DialogMode : u8 {
    /// Blocks the rest of the window until it is closed: an action taken once.
    Modal,
    /// Floats over the rest of the window, which stays usable beside it.
    Floating,
};

/// A window opened from the Tools menu for something done to the checkout,
/// rather than a view of it: it is never docked, and is not part of the layout
/// that is saved. What it holds is kept while it is closed, so opening it
/// again picks up where it was left.
class Dialog {
public:
    /// `width` and `height` are the size it first opens at, in multiples of
    /// the font size.
    Dialog(std::string id, std::string title, const char* icon, DialogMode mode, f32 width,
           f32 height);

    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;
    Dialog(Dialog&&) = delete;
    Dialog& operator=(Dialog&&) = delete;
    virtual ~Dialog() = default;

    [[nodiscard]] const std::string& getId() const {
        return id;
    }

    [[nodiscard]] const std::string& getMenuLabel() const {
        return menuLabel;
    }

    [[nodiscard]] bool isShown() const {
        return shown;
    }

    void show() {
        shown = true;
    }

    /// Draws the window around `draw` while it is shown.
    void drawWindow();

    virtual void draw() = 0;

private:
    std::string id;
    std::string menuLabel;
    std::string windowTitle;
    DialogMode mode;
    f32 width;
    f32 height;
    bool shown = false;
};

} // namespace toki::core
