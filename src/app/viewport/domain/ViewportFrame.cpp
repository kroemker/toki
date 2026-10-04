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
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr u32 FramePhysicalAddress = 0x00F0'0000;
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

FrameList writeFrameList(const ViewportImage& viewportImage, const OrbitCamera& camera,
                         const FrameSize size) {
    const f32 halfWidth = static_cast<f32>(size.width) / 2.0F;
    const f32 halfHeight = static_cast<f32>(size.height) / 2.0F;
    const f32 aspect = static_cast<f32>(size.width) / static_cast<f32>(size.height);

    n64::DisplayListWriter writer(FrameSegment);
    const u32 viewport = writer.addViewport(n64::Viewport{
        .scale = {halfWidth, halfHeight, DepthHalfRange},
        .translate = {halfWidth, halfHeight, DepthHalfRange},
    });
    const u32 projection = writer.addMatrix(makeMatrix(OrbitCamera::makeProjectionMatrix(aspect)));
    const u32 view = writer.addMatrix(makeMatrix(camera.makeViewMatrix()));

    const u32 start = writer.setViewport(viewport);
    writer.loadMatrix(projection, n64::MatrixLoad::Projection);
    writer.loadMatrix(view, n64::MatrixLoad::ModelView);
    writer.syncPipe();
    writer.setGeometryMode(0x00FF'FFFF, n64::geometry::ZBuffer | n64::geometry::Shade |
                                            n64::geometry::SmoothShading);
    for (const u32 displayList : viewportImage.displayLists) {
        writer.callDisplayList(displayList);
    }
    writer.endDisplayList();
    return FrameList{.bytes = writer.getBytes(), .start = start};
}

} // namespace

Frame runFrame(const ViewportImage& viewportImage, const OrbitCamera& camera,
               const FrameSize size) {
    Frame frame;
    if (size.width == 0 || size.height == 0) {
        return frame;
    }
    if (viewportImage.image.findSegment(FrameSegment) != nullptr) {
        frame.diagnostics.push_back(base::makeError(
            {}, std::format("the image uses segment {}, which the viewport draws its frame from",
                            FrameSegment)));
        return frame;
    }

    const FrameList frameList = writeFrameList(viewportImage, camera, size);
    n64::Memory memory;
    std::expected<std::array<u32, n64::SegmentCount>, base::Diagnostic> segments =
        z64::mapImage(viewportImage.image, memory);
    if (!segments) {
        frame.diagnostics.push_back(std::move(segments.error()));
        return frame;
    }
    if (const std::expected<void, base::Diagnostic> mapped =
            memory.map(FramePhysicalAddress, frameList.bytes);
        !mapped) {
        frame.diagnostics.push_back(mapped.error());
        return frame;
    }
    (*segments)[FrameSegment] = FramePhysicalAddress;

    frame.rspResult =
        n64::runTask(memory, n64::RspTask{.displayList = frameList.start, .segments = *segments});
    frame.rdpResult = n64::runRdp(frame.rspResult, memory);
    std::ranges::move(frame.rspResult.diagnostics, std::back_inserter(frame.diagnostics));
    std::ranges::move(frame.rdpResult.diagnostics, std::back_inserter(frame.diagnostics));
    return frame;
}

} // namespace toki::viewport
