#include "Window.h"

#include "app/core/domain/Settings.h"
#include "app/core/ui/Theme.h"
#include "gfx/Device.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace toki::core {

std::expected<Window, std::string> Window::create(const char* title,
                                                  const std::optional<WindowGeometry>& geometry,
                                                  const gfx::Device& device) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return std::unexpected(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    const int width = geometry ? geometry->width : DefaultWidth;
    const int height = geometry ? geometry->height : DefaultHeight;

    Window window;
    window.window =
        SDL_CreateWindow(title, width, height,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (window.window == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return std::unexpected(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    if (!SDL_ClaimWindowForGPUDevice(device.getHandle(), window.window)) {
        SDL_DestroyWindow(window.window);
        window.window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return std::unexpected(
            std::format("the window could not be drawn to by the GPU: {}", SDL_GetError()));
    }
    window.device = &device;
    SDL_SetGPUSwapchainParameters(device.getHandle(), window.window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    if (geometry) {
        SDL_SetWindowPosition(window.window, geometry->x, geometry->y);
        if (geometry->maximized) {
            SDL_MaximizeWindow(window.window);
        }
    }
    SDL_ShowWindow(window.window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForSDLGPU(window.window);
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = device.getHandle();
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device.getHandle(), window.window);
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&info);

    return window;
}

Window::Window(Window&& other) noexcept
    : window(std::exchange(other.window, nullptr)), device(std::exchange(other.device, nullptr)),
      running(std::exchange(other.running, false)), restored(other.restored) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        window = std::exchange(other.window, nullptr);
        device = std::exchange(other.device, nullptr);
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

    SDL_WaitForGPUIdle(device->getHandle());
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device->getHandle(), window);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    device = nullptr;
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

    ImGui_ImplSDLGPU3_NewFrame();
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
    ImDrawData* drawData = ImGui::GetDrawData();

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device->getHandle());
    if (commands == nullptr) {
        return;
    }
    SDL_GPUTexture* swapchain = nullptr;
    const bool minimized = drawData->DisplaySize.x <= 0.0F || drawData->DisplaySize.y <= 0.0F;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, nullptr, nullptr) ||
        swapchain == nullptr || minimized) {
        SDL_SubmitGPUCommandBuffer(commands);
        return;
    }

    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);
    SDL_GPUColorTargetInfo target{};
    target.texture = swapchain;
    target.clear_color = SDL_FColor{.r = palette::Background.x,
                                    .g = palette::Background.y,
                                    .b = palette::Background.z,
                                    .a = palette::Background.w};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(commands);
}

} // namespace toki::core
