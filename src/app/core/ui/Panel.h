#pragma once

#include "app/core/Types.h"

#include <imgui.h>

#include <string>

namespace toki::core {

enum class DockSlot : u8 {
    Centre,
    Left,
    Right,
    Bottom,
};

class Panel {
public:
    Panel(std::string id, std::string title, const char* icon, DockSlot slot);

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    Panel(Panel&&) = delete;
    Panel& operator=(Panel&&) = delete;
    virtual ~Panel() = default;

    [[nodiscard]] const std::string& getId() const {
        return id;
    }

    [[nodiscard]] const std::string& getMenuLabel() const {
        return menuLabel;
    }

    [[nodiscard]] const std::string& getWindowTitle() const {
        return windowTitle;
    }

    [[nodiscard]] DockSlot getDockSlot() const {
        return dockSlot;
    }

    [[nodiscard]] bool isOpen() const {
        return open;
    }

    void setOpen(const bool value) {
        open = value;
    }

    [[nodiscard]] virtual bool isOpenByDefault() const {
        return true;
    }

    [[nodiscard]] virtual ImGuiWindowFlags getWindowFlags() const {
        return ImGuiWindowFlags_None;
    }

    virtual void draw() = 0;

private:
    std::string id;
    std::string menuLabel;
    std::string windowTitle;
    DockSlot dockSlot;
    bool open = true;
};

} // namespace toki::core
