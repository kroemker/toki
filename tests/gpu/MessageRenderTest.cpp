#include "app/message/domain/Message.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageGeometry.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageReader.h"
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
    const std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(workspace.has_value());

    const toki::message::MessageArt art = toki::message::readMessageArt(*workspace);
    const toki::message::MessageGeometry geometry = toki::message::readMessageGeometry(*workspace);
    const toki::message::MessageTable table = toki::message::readMessages(*workspace);
    const std::expected<toki::message::MessageFormat, base::Diagnostic> format =
        toki::message::readMessageFormat(*workspace);
    REQUIRE(format.has_value());
    const toki::message::MessageEncoder encoder = format->makeEncoder();
    const std::filesystem::path output = std::filesystem::path(TOKI_GPU_OUTPUT) / "messages";
    std::filesystem::create_directories(output);

    for (const u16 id :
         {u16{0x0001}, u16{0x0006}, u16{0x0035}, u16{0x1035}, u16{0x4000}, u16{0x00B4}}) {
        const auto message =
            std::ranges::find_if(table.messages, [id](const toki::message::Message& candidate) {
                return candidate.id == id &&
                       toki::message::isInLanguage(candidate, toki::message::Language::English,
                                                   false);
            });
        REQUIRE(message != table.messages.end());
        const toki::message::EncodedText text =
            encoder.encode(message->texts[static_cast<usize>(toki::message::Language::English)]);
        const toki::message::MessageLayout layout = toki::message::layoutMessage(
            text.bytes, geometry, *format->makeLayoutRequest(*message));
        for (usize index = 0; index < layout.boxes.size(); ++index) {
            const toki::message::BoxFrame frame =
                toki::message::runBoxFrame(layout.boxes[index], art);
            CHECK(frame.diagnostics.empty());
            static_cast<void>(renderer->render(frame.rspResult, frame.rdpResult, *target));
            auto pixels = target->readPixels(*device);
            REQUIRE(pixels.has_value());
            for (usize alpha = 3; alpha < pixels->size(); alpha += 4) {
                (*pixels)[alpha] = 0xFF;
            }
            const auto png = z64::encodePng(*pixels, ImageWidth, ImageHeight);
            REQUIRE(png.has_value());
            std::ofstream(output / std::format("{:04X}_{}.png", id, index), std::ios::binary)
                .write(reinterpret_cast<const char*>(png->data()),
                       static_cast<std::streamsize>(png->size()));
        }
    }
}
