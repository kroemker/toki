#include "window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <format>
#include <utility>

namespace toki::core {

namespace {
/// Fast3D's OpenGL backend targets 3.3 core, so the whole app sits on that
/// profile rather than asking for something the renderer cannot use.
constexpr int GlMajorVersion = 3;
constexpr int GlMinorVersion = 3;
constexpr const char* GlslVersion = "#version 330";
} // namespace

std::expected<Window, std::string> Window::create(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return std::unexpected(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Window window;
    window.window_ = SDL_CreateWindow(title, width, height,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window.window_ == nullptr) {
        SDL_Quit();
        return std::unexpected(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    window.gl_context_ = SDL_GL_CreateContext(window.window_);
    if (window.gl_context_ == nullptr) {
        SDL_DestroyWindow(window.window_);
        window.window_ = nullptr;
        SDL_Quit();
        return std::unexpected(std::format("SDL_GL_CreateContext failed: {}", SDL_GetError()));
    }

    SDL_GL_MakeCurrent(window.window_, window.gl_context_);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForOpenGL(window.window_, window.gl_context_);
    ImGui_ImplOpenGL3_Init(GlslVersion);

    return window;
}

Window::Window(Window&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      gl_context_(std::exchange(other.gl_context_, nullptr)),
      running_(std::exchange(other.running_, false)) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        window_ = std::exchange(other.window_, nullptr);
        gl_context_ = std::exchange(other.gl_context_, nullptr);
        running_ = std::exchange(other.running_, false);
    }
    return *this;
}

Window::~Window() {
    destroy();
}

void Window::destroy() noexcept {
    if (window_ == nullptr) {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();

    gl_context_ = nullptr;
    window_ = nullptr;
}

bool Window::begin_frame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const bool closed_this_window =
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window_);
        if (event.type == SDL_EVENT_QUIT || closed_this_window) {
            running_ = false;
        }
    }

    if (!running_) {
        return false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Window::end_frame() {
    ImGui::Render();

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.09F, 0.09F, 0.11F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

} // namespace toki::core
