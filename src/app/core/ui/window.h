#pragma once

#include <expected>
#include <string>

struct SDL_Window;
struct SDL_GLContextState;
using SDL_GLContext = SDL_GLContextState*;

namespace toki::core {

/// Owns the SDL window, its GL context and the ImGui context as one unit,
/// because none of the three is usable without the other two.
class Window {
public:
    [[nodiscard]] static std::expected<Window, std::string> create(const char* title, int width,
                                                                   int height);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    /// Drains events and opens an ImGui frame. Returns false once the user has
    /// asked to close, at which point the frame must not be drawn.
    [[nodiscard]] bool begin_frame();

    void end_frame();

private:
    Window() = default;
    void destroy() noexcept;

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    bool running_ = true;
};

} // namespace toki::core
