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

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ratio>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] f64 makeMilliseconds(const Clock::duration duration) {
    return std::chrono::duration<f64, std::milli>(duration).count();
}

constexpr f32 DepthHalfRange = 511.0F;

struct FrameList {
    std::vector<u8> bytes;
    u32 start = 0;
    /// Where the frame list calls each draw's display list, in the frame's
    /// segment; nothing for a draw left out.
    std::vector<std::optional<u32>> drawCalls;
};

using MappedImages = std::vector<std::optional<z64::MappedImage>>;

[[nodiscard]] bool isMapped(const ViewportDraw& draw, const MappedImages& mapped) {
    const auto isImageMapped = [&mapped](const usize image) { return mapped[image].has_value(); };
    return isImageMapped(draw.image) && std::ranges::all_of(draw.alsoImages, isImageMapped) &&
           std::ranges::all_of(draw.pointedSegments, [&](const SegmentPointer& pointer) {
               return isImageMapped(pointer.image);
           });
}

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

[[nodiscard]] std::vector<std::optional<u32>> addDrawMatrices(n64::DisplayListWriter& writer,
                                                              const ViewportContent& content,
                                                              const glm::mat4& view) {
    std::vector<std::optional<u32>> matrices;
    matrices.reserve(content.draws.size());
    for (const ViewportDraw& draw : content.draws) {
        if (!draw.modelView) {
            matrices.emplace_back();
            continue;
        }
        const glm::mat4 modelView =
            draw.isBillboard ? makeBillboardMatrix(*draw.modelView, view) : *draw.modelView;
        matrices.emplace_back(writer.addMatrix(makeFixedMatrix(modelView)));
    }
    return matrices;
}

struct DrawData {
    std::vector<std::optional<u32>> matrices;
    std::vector<std::optional<u32>> lookAts;
    u32 identity = 0;
    u32 lookAt = 0;
};

[[nodiscard]] u32 addLookAt(n64::DisplayListWriter& writer, const glm::vec3& eye,
                            const glm::vec3& target) {
    const std::array<std::array<s8, 3>, 2> lookAt = makeLookAt(eye, target);
    return writer.addLookAt(lookAt[0], lookAt[1]);
}

[[nodiscard]] std::vector<std::optional<u32>> addDrawLookAts(n64::DisplayListWriter& writer,
                                                             const ViewportContent& content,
                                                             const glm::vec3& eye) {
    std::vector<std::optional<u32>> lookAts;
    lookAts.reserve(content.draws.size());
    for (const ViewportDraw& draw : content.draws) {
        lookAts.push_back(draw.lookAtTarget
                              ? std::optional(addLookAt(writer, eye, *draw.lookAtTarget))
                              : std::nullopt);
    }
    return lookAts;
}

[[nodiscard]] std::vector<std::optional<u32>> writeDraws(n64::DisplayListWriter& writer,
                                                         const ViewportContent& content,
                                                         const MappedImages& mapped,
                                                         const DrawData& data) {
    std::vector<std::optional<u32>> calls(content.draws.size());
    std::optional<usize> current = 0;
    for (usize i = 0; i < content.draws.size(); ++i) {
        const ViewportDraw& draw = content.draws[i];
        if (!isMapped(draw, mapped)) {
            continue;
        }
        for (const usize image : draw.alsoImages) {
            setSegments(writer, *content.images[image], *mapped[image]);
            current.reset();
        }
        if (draw.image != current) {
            setSegments(writer, *content.images[draw.image], *mapped[draw.image]);
            current = draw.image;
        }
        for (const SegmentPointer& pointer : draw.pointedSegments) {
            const n64::SegmentAddress address = n64::unpack(pointer.address);
            writer.setSegment(pointer.segment,
                              mapped[pointer.image]->segments[address.segment] + address.offset);
        }
        if (data.matrices[i]) {
            writer.loadMatrix(*data.matrices[i], n64::MatrixLoad::ModelView);
        }
        if (data.lookAts[i]) {
            writer.setLookAt(*data.lookAts[i]);
        }
        calls[i] = writer.callDisplayList(draw.displayList);
        if (data.matrices[i]) {
            writer.loadMatrix(data.identity, n64::MatrixLoad::ModelView);
        }
        if (data.lookAts[i]) {
            writer.setLookAt(data.lookAt);
        }
    }
    return calls;
}

/// What each draw made, from where the RSP marked the frame list's call of
/// it and the command after.
[[nodiscard]] std::vector<std::optional<OutputRange>>
findDrawOutputs(const n64::RspResult& result, const FrameList& frameList, const u32 physical) {
    std::unordered_map<u32, usize> marks;
    for (usize i = 0; i < result.listMarks.size(); ++i) {
        marks.emplace(result.listMarks[i].address, i);
    }
    std::vector<std::optional<OutputRange>> ranges;
    ranges.reserve(frameList.drawCalls.size());
    for (const std::optional<u32>& call : frameList.drawCalls) {
        const auto found = call ? marks.find(physical + (*call & 0x00FF'FFFF)) : marks.end();
        if (found == marks.end()) {
            ranges.emplace_back();
            continue;
        }
        const usize next = found->second + 1;
        ranges.push_back(OutputRange{.first = result.listMarks[found->second].firstOutput,
                                     .end = next < result.listMarks.size()
                                                ? result.listMarks[next].firstOutput
                                                : result.outputs.size()});
    }
    return ranges;
}

FrameList writeFrameList(const ViewportContent& content, const MappedImages& mapped,
                         const OrbitCamera& camera, const FrameSize size) {
    const glm::mat4 view = camera.makeViewMatrix();
    n64::DisplayListWriter writer(FrameSegment);
    static_cast<void>(
        writer.addMatrix(makeFixedMatrix(makeBillboardMatrix(glm::mat4(1.0F), view))));
    const u32 viewport = writer.addViewport(makeFrameViewport(size));
    const u32 projection =
        writer.addMatrix(makeFixedMatrix(makeViewProjection(content, camera, size)));
    const std::optional<LightAddresses> lights =
        content.lighting ? std::optional(addLights(writer, *content.lighting)) : std::nullopt;
    const DrawData data{
        .matrices = addDrawMatrices(writer, content, view),
        .lookAts = addDrawLookAts(writer, content, camera.getEye()),
        .identity = writer.addMatrix(n64::makeIdentityMatrix()),
        .lookAt = addLookAt(writer, camera.getEye(), camera.getTarget()),
    };

    const u32 start = writer.setViewport(viewport);
    writer.loadMatrix(projection, n64::MatrixLoad::Projection);
    writer.loadMatrix(data.identity, n64::MatrixLoad::ModelView);
    writer.setLookAt(data.lookAt);
    if (lights) {
        setLighting(writer, *content.lighting, *lights);
    }
    writer.syncPipe();
    writer.setGeometryMode(0x00FF'FFFF, n64::geometry::ZBuffer | n64::geometry::Shade |
                                            n64::geometry::SmoothShading);
    for (usize image = content.images.size(); image-- > 0;) {
        if (mapped[image]) {
            setSegments(writer, *content.images[image], *mapped[image]);
        }
    }
    std::vector<std::optional<u32>> calls = writeDraws(writer, content, mapped, data);
    writer.endDisplayList();
    return FrameList{.bytes = writer.getBytes(), .start = start, .drawCalls = std::move(calls)};
}

[[nodiscard]] std::optional<base::Diagnostic> checkContent(const ViewportContent& content) {
    for (const std::shared_ptr<const z64::MemoryImage>& image : content.images) {
        if (image == nullptr || image->findSegment(FrameSegment) != nullptr) {
            return base::makeError(
                {}, std::format("an image uses segment {}, which the viewport draws its frame from",
                                FrameSegment));
        }
    }
    for (const ViewportDraw& draw : content.draws) {
        const auto isImage = [&content](const usize image) {
            return image < content.images.size();
        };
        const bool isKnown =
            isImage(draw.image) && std::ranges::all_of(draw.alsoImages, isImage) &&
            std::ranges::all_of(draw.pointedSegments, [&](const SegmentPointer& pointer) {
                return isImage(pointer.image);
            });
        if (!isKnown) {
            return base::makeError({}, std::format("a draw names an image past the content's {}",
                                                   content.images.size()));
        }
    }
    return std::nullopt;
}

} // namespace

std::array<std::array<s8, 3>, 2> makeLookAt(const glm::vec3& eye, const glm::vec3& target) {
    const glm::vec3 look = glm::normalize(target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0F, 1.0F, 0.0F), look));
    const glm::vec3 up = glm::normalize(glm::cross(look, right));
    const auto scale = [](const glm::vec3& direction) {
        std::array<s8, 3> scaled{};
        for (int axis = 0; axis < 3; ++axis) {
            scaled[axis] = static_cast<s8>(std::min(direction[axis] * 128.0F, 127.0F));
        }
        return scaled;
    };
    return {scale(right), scale(up)};
}

glm::mat4 makeBillboardMatrix(const glm::mat4& model, const glm::mat4& view) {
    const glm::mat3 facing = glm::transpose(glm::mat3(view));
    glm::mat4 billboard(1.0F);
    for (int axis = 0; axis < 3; ++axis) {
        billboard[axis] = glm::vec4(facing[axis] * glm::length(glm::vec3(model[axis])), 0.0F);
    }
    billboard[3] = model[3];
    return billboard;
}

n64::FixedMatrix makeFixedMatrix(const glm::mat4& matrix) {
    std::array<f32, 16> elements{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            elements[(static_cast<usize>(row) * 4) + static_cast<usize>(column)] =
                matrix[row][column];
        }
    }
    return n64::makeFixedMatrix(elements);
}

namespace {

/// The content's images mapped one after another from `FirstImageAddress`,
/// leaving out each that would end past a limit, and the frame's own display
/// list after them.
struct PlacedFrame {
    n64::Memory memory;
    MappedImages mapped;
    usize leftOut = 0;
    FrameList frameList;
    u32 frameListAddress = 0;
};

[[nodiscard]] std::expected<void, base::Diagnostic>
mapImages(const ViewportContent& content, PlacedFrame& placed, const u64 limit) {
    u32 physical = FirstImageAddress;
    for (const std::shared_ptr<const z64::MemoryImage>& image : content.images) {
        if (z64::findMappedEnd(*image, physical) > limit) {
            placed.mapped.emplace_back();
            ++placed.leftOut;
            continue;
        }
        std::expected<z64::MappedImage, base::Diagnostic> mapped =
            z64::mapImageAt(*image, placed.memory, physical);
        if (!mapped) {
            return std::unexpected(std::move(mapped.error()));
        }
        physical = mapped->end;
        placed.mapped.emplace_back(*mapped);
    }
    placed.frameListAddress = physical;
    return {};
}

[[nodiscard]] std::expected<PlacedFrame, base::Diagnostic>
placeFrame(const ViewportContent& content, const OrbitCamera& camera, const FrameSize size) {
    u64 limit = n64::AddressSpaceSize;
    while (true) {
        PlacedFrame placed;
        if (std::expected<void, base::Diagnostic> mapped = mapImages(content, placed, limit);
            !mapped) {
            return std::unexpected(std::move(mapped.error()));
        }
        placed.frameList = writeFrameList(content, placed.mapped, camera, size);
        const u64 listSize = placed.frameList.bytes.size();
        if (placed.frameListAddress + listSize <= n64::AddressSpaceSize) {
            if (std::expected<void, base::Diagnostic> mapped =
                    placed.memory.map(placed.frameListAddress, placed.frameList.bytes);
                !mapped) {
                return std::unexpected(std::move(mapped.error()));
            }
            return placed;
        }
        if (listSize >= n64::AddressSpaceSize - FirstImageAddress ||
            n64::AddressSpaceSize - listSize >= limit) {
            return std::unexpected(base::makeError(
                {}, std::format("the frame's display list of {} bytes fits nowhere in the 16 MiB "
                                "the RSP addresses",
                                listSize)));
        }
        limit = n64::AddressSpaceSize - listSize;
    }
}

} // namespace

n64::Viewport makeFrameViewport(const FrameSize size) {
    const f32 halfWidth = static_cast<f32>(size.width) / 2.0F;
    const f32 halfHeight = static_cast<f32>(size.height) / 2.0F;
    return n64::Viewport{
        .scale = {halfWidth, halfHeight, DepthHalfRange},
        .translate = {halfWidth, halfHeight, DepthHalfRange},
    };
}

glm::mat4 makeViewProjection(const ViewportContent& content, const OrbitCamera& camera,
                             const FrameSize size) {
    const f32 aspect = static_cast<f32>(size.width) / static_cast<f32>(size.height);
    const f32 zFar = content.lighting ? content.lighting->zFar : OrbitCamera::Far;
    return OrbitCamera::makeProjectionMatrix(aspect, zFar) * camera.makeViewMatrix();
}

Frame runFrame(const ViewportContent& content, const OrbitCamera& camera, const FrameSize size,
               const bool recordsTrace, const n64::RspOverrides& overrides) {
    Frame frame;
    if (size.width == 0 || size.height == 0) {
        return frame;
    }
    if (std::optional<base::Diagnostic> refused = checkContent(content)) {
        frame.diagnostics.push_back(std::move(*refused));
        return frame;
    }

    const Clock::time_point started = Clock::now();
    std::expected<PlacedFrame, base::Diagnostic> placed = placeFrame(content, camera, size);
    if (!placed) {
        frame.diagnostics.push_back(std::move(placed.error()));
        return frame;
    }
    if (placed->leftOut > 0) {
        frame.diagnostics.push_back(base::makeError(
            {}, std::format("{} of the {} images to draw do not fit in the 16 MiB the RSP "
                            "addresses, so what is drawn from them is left out",
                            placed->leftOut, content.images.size())));
    }
    const n64::Memory& memory = placed->memory;
    const u32 physical = placed->frameListAddress;
    const FrameList& frameList = placed->frameList;
    std::array<u32, n64::SegmentCount> segments{};
    segments[FrameSegment] = physical;
    frame.mappedImages = placed->mapped;
    frame.frameListAddress = physical;
    frame.frameListSize = frameList.bytes.size();

    const Clock::time_point placedAt = Clock::now();
    frame.rspResult = n64::runTask(memory, n64::RspTask{.displayList = frameList.start,
                                                        .segments = segments,
                                                        .recordsTrace = recordsTrace,
                                                        .overrides = overrides});
    const Clock::time_point ranAt = Clock::now();
    frame.drawOutputs = findDrawOutputs(frame.rspResult, frameList, physical);
    frame.rdpResult = n64::runRdp(frame.rspResult, memory);
    frame.timings = FrameTimings{.place = makeMilliseconds(placedAt - started),
                                 .rsp = makeMilliseconds(ranAt - placedAt),
                                 .rdp = makeMilliseconds(Clock::now() - ranAt)};
    std::ranges::move(frame.rspResult.diagnostics, std::back_inserter(frame.diagnostics));
    std::ranges::move(frame.rdpResult.diagnostics, std::back_inserter(frame.diagnostics));
    return frame;
}

} // namespace toki::viewport
