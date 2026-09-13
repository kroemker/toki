#include "app/core/ui/window.h"

#include <imgui.h>

#include <cstdio>
#include <print>

int main() {
    auto window = toki::core::Window::create("Toki64", 1600, 900);
    if (!window) {
        std::println(stderr, "{}", window.error());
        return 1;
    }

    while (window->begin_frame()) {
        ImGui::DockSpaceOverViewport();
        ImGui::ShowDemoWindow();
        window->end_frame();
    }

    return 0;
}
