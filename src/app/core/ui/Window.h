#pragma once

#include "app/core/domain/Settings.h"

#include <expected>
#include <optional>
#include <string>
#include <utility>

struct SDL_Window;

namespace gfx {
class Device;
} // namespace gfx

namespace toki::core {

class Window {
public:
    static constexpr int DefaultWidth = 1600;
    static constexpr int DefaultHeight = 900;

    [[nodiscard]] static std::expected<Window, std::string>
    create(const char* title, const std::optional<WindowGeometry>& geometry,
           const gfx::Device& device);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    [[nodiscard]] bool beginFrame();

    void endFrame();

    /// Ends the frames: the window closes once the frame drawn now is done.
    void requestClose() {
        running = false;
    }

    /// Whether the person asked to close the window since the last call, as
    /// its close button or Alt+F4 does; the window stays open until
    /// `requestClose`, so that what would be lost can be asked about first.
    [[nodiscard]] bool takeCloseRequest() {
        return std::exchange(closeRequested, false);
    }

    [[nodiscard]] SDL_Window* getHandle() const {
        return window;
    }

    [[nodiscard]] WindowGeometry getGeometry() const;

private:
    Window() = default;
    void destroy() noexcept;
    void rememberGeometry();

    SDL_Window* window = nullptr;
    const gfx::Device* device = nullptr;
    bool running = true;
    bool closeRequested = false;
    WindowGeometry restored{
        .x = 0, .y = 0, .width = DefaultWidth, .height = DefaultHeight, .maximized = false};
};

} // namespace toki::core
