#include "app/core/domain/Settings.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionDrawing.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneView.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/OverlayGeometry.h"
#include "app/viewport/domain/SelectionShape.h"
#include "app/viewport/domain/ViewportChoices.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "gfx/Device.h"
#include "gfx/Overlay.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Gbi.h"
#include "hackeroot/Workspace.h"
#include "z64/PngImage.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using base::f32;
using base::u32;
using base::usize;

namespace {

constexpr u32 ImageWidth = 640;
constexpr u32 ImageHeight = 480;

/// The value of the environment variable `name`, empty when it is unset.
[[nodiscard]] std::string findEnvironment(const char* name) {
    std::string text;
#ifdef _MSC_VER
    char* value = nullptr;
    usize length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        text = value;
        std::free(value);
    }
#else
    if (const char* value = std::getenv(name)) {
        text = value;
    }
#endif
    return text;
}

/// The scenes `TOKI_RENDER_SCENES` names, separated by commas, or a few that
/// show collision of every kind when it is unset.
[[nodiscard]] std::vector<std::string> listWantedScenes() {
    const std::string text = findEnvironment("TOKI_RENDER_SCENES");
    if (text.empty()) {
        return {"spot00", "spot01", "kakusiana", "ydan"};
    }
    std::vector<std::string> names;
    usize start = 0;
    while (start <= text.size()) {
        const usize end = std::min(text.find(',', start), text.size());
        if (end > start) {
            names.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return names;
}

/// The camera looking down at the middle of `collision` from far enough to
/// see all of it.
[[nodiscard]] toki::viewport::OrbitCamera makeCamera(const toki::scene::Collision& collision) {
    const glm::vec3 lowest{collision.minBounds[0], collision.minBounds[1], collision.minBounds[2]};
    const glm::vec3 highest{collision.maxBounds[0], collision.maxBounds[1], collision.maxBounds[2]};
    toki::viewport::OrbitCamera camera;
    camera.setTarget((lowest + highest) * 0.5F);
    camera.setDistance(std::min(glm::length(highest - lowest) * 0.6F, 6000.0F));
    camera.setAngles(30.0F, 40.0F);
    return camera;
}

} // namespace

TEST_CASE("scenes are drawn to images with their collision over the rooms and alone", "[.render]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    std::expected<gfx::Device, base::Diagnostic> device = gfx::Device::create();
    if (!device) {
        SKIP(device.error().message);
    }
    std::expected<gfx::Renderer, base::Diagnostic> renderer = gfx::Renderer::create(*device);
    REQUIRE(renderer.has_value());
    std::expected<gfx::RenderTarget, base::Diagnostic> target =
        gfx::RenderTarget::create(*device, ImageWidth, ImageHeight);
    REQUIRE(target.has_value());
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(loaded.has_value());
    if (const std::optional<base::Diagnostic> unsupported =
            hackeroot::findUnsupportedMicrocode(*loaded)) {
        SKIP(unsupported->message);
    }
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    const std::filesystem::path output = std::filesystem::path(TOKI_GPU_OUTPUT) / "scene-renders";
    std::error_code failure;
    std::filesystem::create_directories(output, failure);

    for (const std::string& name : listWantedScenes()) {
        const auto& scenes = workspace->getAssets().scenes;
        const auto asset = std::ranges::find(scenes, name, &hackeroot::AssetDirectory::name);
        INFO("TOKI_RENDER_SCENES names " << name);
        REQUIRE(asset != scenes.end());
        hackeroot::AssetImageReader reader(workspace);
        const toki::scene::Scene scene = toki::scene::readScene(reader, *asset);
        REQUIRE(scene.layers.front().collision.has_value());
        toki::viewport::OrbitCamera camera =
            makeCamera(scene.collisions[*scene.layers.front().collision]);
        if (const std::string distance = findEnvironment("TOKI_RENDER_DISTANCE");
            !distance.empty()) {
            camera.setDistance(std::stof(distance));
        }
        toki::scene::SceneView view = toki::scene::makeSceneView(scene);
        view.showsActors = false;
        for (const int display : {0, 1, 2}) {
            view.showsCollision = display != 0;
            std::ranges::fill(view.hiddenRooms, display == 2);
            const toki::viewport::ViewportContent content =
                toki::scene::makeSceneContent(scene, view, toki::core::MarkerShape::Cube);
            const toki::viewport::FrameSize size{.width = ImageWidth, .height = ImageHeight};
            const toki::viewport::Frame frame = toki::viewport::runFrame(content, camera, size);
            gfx::Overlay overlay{.viewport = toki::viewport::makeFrameViewport(size)};
            for (usize i = 0; i < content.draws.size(); ++i) {
                if (content.draws[i].isEdged) {
                    std::ranges::move(toki::viewport::makeFaceEdges(
                                          toki::viewport::listDrawTriangles(
                                              frame.rspResult, frame.drawOutputs, {&i, 1}),
                                          {.color = {20, 21, 25, 200}, .width = 1.0F}),
                                      std::back_inserter(overlay.lines));
                }
            }
            static_cast<void>(renderer->render(frame.rspResult, frame.rdpResult, *target,
                                               {.width = ImageWidth, .height = ImageHeight},
                                               overlay));
            auto pixels = target->readPixels(*device);
            REQUIRE(pixels.has_value());
            for (usize alpha = 3; alpha < pixels->size(); alpha += 4) {
                (*pixels)[alpha] = 0xFF;
            }
            const auto png = z64::encodePng(*pixels, ImageWidth, ImageHeight);
            REQUIRE(png.has_value());
            std::ofstream(output / std::format("{}_{}.png", name, display), std::ios::binary)
                .write(reinterpret_cast<const char*>(png->data()),
                       static_cast<std::streamsize>(png->size()));
        }
    }
}
