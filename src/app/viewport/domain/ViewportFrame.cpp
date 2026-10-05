#include "ViewportFrame.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "base/Diagnostic.h"
#include "n64/DisplayListWriter.h"
#include "n64/FixedMatrix.h"
#include "n64/Memory.h"
#include "n64/Opcode.h"
#include "n64/Rdp.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"
#include "n64/Segment.h"
#include "n64/Viewport.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr u32 FirstPhysicalAddress = 0x0010'0000;
constexpr f32 DepthHalfRange = 511.0F;

n64::FixedMatrix makeMatrix(const glm::mat4& matrix) {
    std::array<f32, 16> elements{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            elements[(static_cast<usize>(row) * 4) + static_cast<usize>(column)] =
                matrix[row][column];
        }
    }
    return n64::makeFixedMatrix(elements);
}

struct FrameList {
    std::vector<u8> bytes;
    u32 start = 0;
};

void setSegments(n64::DisplayListWriter& writer, const z64::MemoryImage& image,
                 const z64::MappedImage& mapped) {
    for (const z64::ImageSegment& segment : image.segments) {
        writer.setSegment(segment.number, mapped.segments[segment.number]);
    }
}

struct LightAddresses {
    std::array<u32, 2> lights{};
    u32 ambient = 0;
};

LightAddresses addLights(n64::DisplayListWriter& writer, const ViewportLighting& lighting) {
    LightAddresses addresses;
    for (usize i = 0; i < lighting.lights.size(); ++i) {
        addresses.lights[i] = writer.addLight(n64::LightData{
            .color = lighting.lights[i].color, .direction = lighting.lights[i].direction});
    }
    addresses.ambient = writer.addLight(n64::LightData{.color = lighting.ambient});
    return addresses;
}

void setFog(n64::DisplayListWriter& writer, const s16 fogNear) {
    constexpr s32 FogFar = 1000;
    if (fogNear >= FogFar) {
        writer.setFogFactor(0, 0);
    } else if (fogNear > 996) {
        writer.setFogFactor(0x7FFF, -0x7F00);
    } else if (fogNear < 0) {
        writer.setFogFactor(0, 255);
    } else {
        const s32 range = FogFar - fogNear;
        writer.setFogFactor(static_cast<s16>(128000 / range),
                            static_cast<s16>((500 - fogNear) * 256 / range));
    }
}

void setLighting(n64::DisplayListWriter& writer, const ViewportLighting& lighting,
                 const LightAddresses& addresses) {
    writer.setLightCount(static_cast<u32>(addresses.lights.size()));
    for (usize i = 0; i < addresses.lights.size(); ++i) {
        writer.setLight(addresses.lights[i], static_cast<u32>(i + 1));
    }
    writer.setLight(addresses.ambient, static_cast<u32>(addresses.lights.size() + 1));
    writer.setFogColor({lighting.fogColor[0], lighting.fogColor[1], lighting.fogColor[2], 0});
    setFog(writer, lighting.fogNear);
}

FrameList writeFrameList(const ViewportContent& content,
                         const std::vector<z64::MappedImage>& mapped, const OrbitCamera& camera,
                         const FrameSize size) {
    const f32 halfWidth = static_cast<f32>(size.width) / 2.0F;
    const f32 halfHeight = static_cast<f32>(size.height) / 2.0F;
    const f32 aspect = static_cast<f32>(size.width) / static_cast<f32>(size.height);

    n64::DisplayListWriter writer(FrameSegment);
    const u32 viewport = writer.addViewport(n64::Viewport{
        .scale = {halfWidth, halfHeight, DepthHalfRange},
        .translate = {halfWidth, halfHeight, DepthHalfRange},
    });
    const f32 zFar = content.lighting ? content.lighting->zFar : OrbitCamera::Far;
    const u32 projection = writer.addMatrix(
        makeMatrix(OrbitCamera::makeProjectionMatrix(aspect, zFar) * camera.makeViewMatrix()));
    const std::optional<LightAddresses> lights =
        content.lighting ? std::optional(addLights(writer, *content.lighting)) : std::nullopt;
    const u32 identity = writer.addMatrix(n64::makeIdentityMatrix());

    const u32 start = writer.setViewport(viewport);
    writer.loadMatrix(projection, n64::MatrixLoad::Projection);
    writer.loadMatrix(identity, n64::MatrixLoad::ModelView);
    if (lights) {
        setLighting(writer, *content.lighting, *lights);
    }
    writer.syncPipe();
    writer.setGeometryMode(0x00FF'FFFF, n64::geometry::ZBuffer | n64::geometry::Shade |
                                            n64::geometry::SmoothShading);
    for (usize image = content.images.size(); image-- > 0;) {
        setSegments(writer, content.images[image], mapped[image]);
    }
    usize current = 0;
    for (const ViewportDraw& draw : content.draws) {
        if (draw.image != current) {
            setSegments(writer, content.images[draw.image], mapped[draw.image]);
            current = draw.image;
        }
        writer.callDisplayList(draw.displayList);
    }
    writer.endDisplayList();
    return FrameList{.bytes = writer.getBytes(), .start = start};
}

[[nodiscard]] std::optional<base::Diagnostic> checkContent(const ViewportContent& content) {
    for (const z64::MemoryImage& image : content.images) {
        if (image.findSegment(FrameSegment) != nullptr) {
            return base::makeError(
                {}, std::format("an image uses segment {}, which the viewport draws its frame from",
                                FrameSegment));
        }
    }
    for (const ViewportDraw& draw : content.draws) {
        if (draw.image >= content.images.size()) {
            return base::makeError(
                {}, std::format("a draw names image {}, of {}", draw.image, content.images.size()));
        }
    }
    return std::nullopt;
}

} // namespace

Frame runFrame(const ViewportContent& content, const OrbitCamera& camera, const FrameSize size) {
    Frame frame;
    if (size.width == 0 || size.height == 0) {
        return frame;
    }
    if (std::optional<base::Diagnostic> refused = checkContent(content)) {
        frame.diagnostics.push_back(std::move(*refused));
        return frame;
    }

    n64::Memory memory;
    std::vector<z64::MappedImage> mapped;
    u32 physical = FirstPhysicalAddress;
    for (const z64::MemoryImage& image : content.images) {
        std::expected<z64::MappedImage, base::Diagnostic> placed =
            z64::mapImageAt(image, memory, physical);
        if (!placed) {
            frame.diagnostics.push_back(std::move(placed.error()));
            return frame;
        }
        physical = placed->end;
        mapped.push_back(*placed);
    }

    const FrameList frameList = writeFrameList(content, mapped, camera, size);
    if (const std::expected<void, base::Diagnostic> placed = memory.map(physical, frameList.bytes);
        !placed) {
        frame.diagnostics.push_back(placed.error());
        return frame;
    }
    std::array<u32, n64::SegmentCount> segments{};
    segments[FrameSegment] = physical;

    frame.rspResult =
        n64::runTask(memory, n64::RspTask{.displayList = frameList.start, .segments = segments});
    frame.rdpResult = n64::runRdp(frame.rspResult, memory);
    std::ranges::move(frame.rspResult.diagnostics, std::back_inserter(frame.diagnostics));
    std::ranges::move(frame.rdpResult.diagnostics, std::back_inserter(frame.diagnostics));
    return frame;
}

} // namespace toki::viewport
