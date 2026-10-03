#include "Window.h"

#include "app/core/domain/Settings.h"
#include "app/core/ui/Theme.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace toki::core {

namespace {

constexpr int GlMajorVersion = 3;
constexpr int GlMinorVersion = 3;
constexpr const char* GlslVersion = "#version 330";
} // namespace

std::expected<Window, std::string> Window::create(const char* title,
                                                  const std::optional<WindowGeometry>& geometry) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return std::unexpected(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    const int width = geometry ? geometry->width : DefaultWidth;
    const int height = geometry ? geometry->height : DefaultHeight;

    Window window;
    window.window = SDL_CreateWindow(title, width, height,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (window.window == nullptr) {
        SDL_Quit();
        return std::unexpected(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    window.glContext = SDL_GL_CreateContext(window.window);
    if (window.glContext == nullptr) {
        SDL_DestroyWindow(window.window);
        window.window = nullptr;
        SDL_Quit();
        return std::unexpected(std::format("SDL_GL_CreateContext failed: {}", SDL_GetError()));
    }

    if (geometry) {
        SDL_SetWindowPosition(window.window, geometry->x, geometry->y);
        if (geometry->maximized) {
            SDL_MaximizeWindow(window.window);
        }
    }
    SDL_ShowWindow(window.window);

    SDL_GL_MakeCurrent(window.window, window.glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForOpenGL(window.window, window.glContext);
    ImGui_ImplOpenGL3_Init(GlslVersion);

    return window;
}

Window::Window(Window&& other) noexcept
    : window(std::exchange(other.window, nullptr)),
      glContext(std::exchange(other.glContext, nullptr)),
      running(std::exchange(other.running, false)), restored(other.restored) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        window = std::exchange(other.window, nullptr);
        glContext = std::exchange(other.glContext, nullptr);
        running = std::exchange(other.running, false);
        restored = other.restored;
    }
    return *this;
}

Window::~Window() {
    destroy();
}

void Window::destroy() noexcept {
    if (window == nullptr) {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    glContext = nullptr;
    window = nullptr;
}

bool Window::beginFrame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const bool closedThisWindow = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                      event.window.windowID == SDL_GetWindowID(window);
        if (event.type == SDL_EVENT_QUIT || closedThisWindow) {
            running = false;
        }
    }

    if (!running) {
        return false;
    }

    rememberGeometry();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Window::rememberGeometry() {
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
        return;
    }

    SDL_GetWindowPosition(window, &restored.x, &restored.y);
    SDL_GetWindowSize(window, &restored.width, &restored.height);
}

WindowGeometry Window::getGeometry() const {
    WindowGeometry current = restored;
    current.maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    return current;
}

void Window::endFrame() {
    ImGui::Render();

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(palette::Background.x, palette::Background.y, palette::Background.z,
                 palette::Background.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

} // namespace toki::core
