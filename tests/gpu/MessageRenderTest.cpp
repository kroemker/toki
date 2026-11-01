#include "app/message/domain/Message.h"
#include "app/message/domain/MessageCatalog.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageLayout.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "hackeroot/Workspace.h"
#include "z64/PngImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using base::u16;
using base::u32;
using base::usize;

namespace {

constexpr u32 ImageWidth = 640;
constexpr u32 ImageHeight = 480;

} // namespace

TEST_CASE("messages are drawn to images box by box", "[.render]") {
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
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageCatalog catalog = toki::message::readMessageCatalog(
        std::make_shared<const hackeroot::Workspace>(std::move(*workspace)), TOKI_SHIPPED_SHIFT_JIS,
        true);
    const std::filesystem::path output = std::filesystem::path(TOKI_GPU_OUTPUT) / "messages";
    std::filesystem::create_directories(output);

    for (const toki::message::Language language :
         {toki::message::Language::English, toki::message::Language::Japanese}) {
        for (const u16 id :
             {u16{0x0001}, u16{0x0006}, u16{0x0035}, u16{0x1035}, u16{0x4000}, u16{0x00B4}}) {
            const auto message = std::ranges::find_if(
                catalog.table.messages, [&](const toki::message::Message& candidate) {
                    return candidate.id == id &&
                           toki::message::isInLanguage(candidate, language, false);
                });
            REQUIRE(message != catalog.table.messages.end());
            const toki::message::MessageLayout layout = catalog.layOut(*message, language);
            for (usize index = 0; index < layout.boxes.size(); ++index) {
                const toki::message::BoxFrame frame =
                    toki::message::runBoxFrame(layout.boxes[index], catalog.art);
                CHECK(frame.diagnostics.empty());
                static_cast<void>(renderer->render(frame.rspResult, frame.rdpResult, *target));
                auto pixels = target->readPixels(*device);
                REQUIRE(pixels.has_value());
                for (usize alpha = 3; alpha < pixels->size(); alpha += 4) {
                    (*pixels)[alpha] = 0xFF;
                }
                const auto png = z64::encodePng(*pixels, ImageWidth, ImageHeight);
                REQUIRE(png.has_value());
                std::ofstream(output / std::format("{}_{:04X}_{}.png",
                                                   toki::message::getLanguageName(language), id,
                                                   index),
                              std::ios::binary)
                    .write(reinterpret_cast<const char*>(png->data()),
                           static_cast<std::streamsize>(png->size()));
            }
        }
    }
}
