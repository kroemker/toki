#pragma once

#include "app/core/domain/Settings.h"

#include <expected>
#include <optional>
#include <string>

struct SDL_Window;
struct SDL_GLContextState;
using SDL_GLContext = SDL_GLContextState*;

namespace toki::core {

class Window {
public:
    static constexpr int DefaultWidth = 1600;
    static constexpr int DefaultHeight = 900;

    [[nodiscard]] static std::expected<Window, std::string>
    create(const char* title, const std::optional<WindowGeometry>& geometry);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    [[nodiscard]] bool beginFrame();

    void endFrame();

    void requestClose() {
        running = false;
    }

    [[nodiscard]] SDL_Window* handle() const {
        return window;
    }

    [[nodiscard]] WindowGeometry geometry() const;

private:
    Window() = default;
    void destroy() noexcept;
    void rememberGeometry();

    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    bool running = true;
    WindowGeometry restored{
        .x = 0, .y = 0, .width = DefaultWidth, .height = DefaultHeight, .maximized = false};
};

} // namespace toki::core
