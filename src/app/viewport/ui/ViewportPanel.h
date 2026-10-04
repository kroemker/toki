#pragma once

#include "app/core/ui/Panel.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"

#include <imgui.h>

#include <optional>
#include <string>

namespace toki::viewport {

class ViewportPanel : public core::Panel {
public:
    explicit ViewportPanel(const gfx::Device& device);

    [[nodiscard]] ImGuiWindowFlags getWindowFlags() const override {
        return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    void setImage(ViewportImage image);

    void draw() override;

private:
    [[nodiscard]] bool handleInput(ImVec2 size);
    void render(const gfx::RenderTarget& target);

    const gfx::Device& device;
    std::optional<gfx::Renderer> renderer;
    std::optional<gfx::RenderTarget> renderTarget;
    std::string failure;
    OrbitCamera orbitCamera;
    ViewportImage viewportImage;
    bool stale = true;
    bool reported = false;
    usize diagnosticCount = 0;
};

} // namespace toki::viewport
