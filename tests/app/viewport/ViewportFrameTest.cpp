#include "app/viewport/domain/ViewportFrame.h"

#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ReferenceGrid.h"
#include "base/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/Opcode.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <memory>
#include <variant>
#include <vector>

using base::f32;
using base::u32;
using base::u8;
using toki::viewport::FrameSize;
using toki::viewport::OrbitCamera;
using toki::viewport::ViewportContent;
using toki::viewport::ViewportDraw;

namespace {

constexpr u8 Segment = 0x06;

std::vector<n64::Triangle> findTriangles(const n64::RspResult& result) {
    std::vector<n64::Triangle> triangles;
    for (const n64::RspOutput& output : result.outputs) {
        if (const auto* triangle = std::get_if<n64::Triangle>(&output)) {
            triangles.push_back(*triangle);
        }
    }
    return triangles;
}

z64::MemoryImage makeImage(const u8 segment, const n64::DisplayListWriter& writer) {
    z64::MemoryImage image;
    image.segments.push_back(z64::ImageSegment{.number = segment, .bytes = writer.getBytes()});
    return image;
}

ViewportContent makeContent(const n64::DisplayListWriter& writer, const u32 start) {
    ViewportContent content;
    content.images.push_back(std::make_shared<const z64::MemoryImage>(makeImage(Segment, writer)));
    content.draws.push_back(ViewportDraw{.image = 0, .displayList = start});
    return content;
}

ViewportContent makeTriangleAt(const glm::vec3& corner) {
    const auto x = static_cast<base::s16>(corner.x);
    const auto y = static_cast<base::s16>(corner.y);
    const auto z = static_cast<base::s16>(corner.z);
    n64::DisplayListWriter writer(Segment);
    const u32 vertices = writer.addVertex({.position = {x, y, z}, .color = {255, 255, 255, 255}});
    static_cast<void>(writer.addVertex(
        {.position = {static_cast<base::s16>(x + 50), y, z}, .color = {255, 255, 255, 255}}));
    static_cast<void>(writer.addVertex(
        {.position = {x, static_cast<base::s16>(y + 50), z}, .color = {255, 255, 255, 255}}));
    const u32 start = writer.loadVertices(vertices, 3, 0);
    writer.drawTriangle({0, 1, 2});
    writer.endDisplayList();
    return makeContent(writer, start);
}

} // namespace

TEST_CASE("the camera's target is drawn at the centre of the screen", "[viewport]") {
    OrbitCamera camera;
    camera.pan(0.1F, -0.05F);
    const glm::vec3 target = camera.getTarget();

    const toki::viewport::Frame frame = toki::viewport::runFrame(
        makeTriangleAt(target), camera, FrameSize{.width = 1280, .height = 720});

    REQUIRE(frame.diagnostics.empty());
    const std::vector<n64::Triangle> triangles = findTriangles(frame.rspResult);
    REQUIRE(triangles.size() == 1);
    const std::array<f32, 4>& position = triangles[0].vertices[0].position;
    REQUIRE(position[0] / position[3] == Catch::Approx(0.0F).margin(0.01));
    REQUIRE(position[1] / position[3] == Catch::Approx(0.0F).margin(0.01));
    REQUIRE(frame.rspResult.viewports.back().scale[0] == Catch::Approx(640.0F));
    REQUIRE(frame.rspResult.viewports.back().translate[1] == Catch::Approx(360.0F));
}

TEST_CASE("the frame's matrices match the camera's within fixed point", "[viewport]") {
    OrbitCamera camera;
    camera.rotate(30.0F, 10.0F);
    const glm::vec3 corner{120.0F, 40.0F, -300.0F};
    const f32 aspect = 4.0F / 3.0F;
    const glm::vec4 expected = OrbitCamera::makeProjectionMatrix(aspect) * camera.makeViewMatrix() *
                               glm::vec4(corner, 1.0F);

    const toki::viewport::Frame frame = toki::viewport::runFrame(
        makeTriangleAt(corner), camera, FrameSize{.width = 640, .height = 480});

    const std::vector<n64::Triangle> triangles = findTriangles(frame.rspResult);
    REQUIRE(triangles.size() == 1);
    const std::array<f32, 4>& position = triangles[0].vertices[0].position;
    REQUIRE(position[0] / position[3] == Catch::Approx(expected.x / expected.w).margin(0.005));
    REQUIRE(position[1] / position[3] == Catch::Approx(expected.y / expected.w).margin(0.005));
    REQUIRE(position[3] == Catch::Approx(expected.w).epsilon(0.01));
}

TEST_CASE("segment 1 starts with the camera's billboard matrix, as the game's does", "[viewport]") {
    OrbitCamera camera;
    camera.rotate(40.0F, 15.0F);
    const glm::vec3 corner{0.0F, 0.0F, 100.0F};
    n64::DisplayListWriter writer(Segment);
    const u32 vertices = writer.addVertex({.position = {0, 0, 100}, .color = {255, 255, 255, 255}});
    static_cast<void>(writer.addVertex({.position = {50, 0, 100}, .color = {255, 255, 255, 255}}));
    static_cast<void>(writer.addVertex({.position = {0, 50, 100}, .color = {255, 255, 255, 255}}));
    const u32 start = writer.getAddress();
    writer.loadMatrix(0x0100'0000, n64::MatrixLoad::ModelView);
    static_cast<void>(writer.loadVertices(vertices, 3, 0));
    writer.drawTriangle({0, 1, 2});
    writer.endDisplayList();
    const glm::mat4 view = camera.makeViewMatrix();
    const glm::vec4 expected = OrbitCamera::makeProjectionMatrix(4.0F / 3.0F) * view *
                               toki::viewport::makeBillboardMatrix(glm::mat4(1.0F), view) *
                               glm::vec4(corner, 1.0F);

    const toki::viewport::Frame frame = toki::viewport::runFrame(
        makeContent(writer, start), camera, FrameSize{.width = 640, .height = 480});

    const std::vector<n64::Triangle> triangles = findTriangles(frame.rspResult);
    REQUIRE(triangles.size() == 1);
    const std::array<f32, 4>& position = triangles[0].vertices[0].position;
    REQUIRE(position[0] / position[3] == Catch::Approx(expected.x / expected.w).margin(0.005));
    REQUIRE(position[1] / position[3] == Catch::Approx(expected.y / expected.w).margin(0.005));
}

TEST_CASE("the reference grid draws every cell and nothing else", "[viewport]") {
    const toki::viewport::Frame frame = toki::viewport::runFrame(
        toki::viewport::makeReferenceGrid(), OrbitCamera{}, FrameSize{.width = 800, .height = 600});

    REQUIRE(frame.diagnostics.empty());
    REQUIRE(findTriangles(frame.rspResult).size() == 16 * 16 * 2);
    REQUIRE(frame.rdpResult.draws.size() == 16 * 16 * 2);
}

TEST_CASE("an image that uses the frame's own segment is refused", "[viewport]") {
    n64::DisplayListWriter writer(toki::viewport::FrameSegment);
    const u32 start = writer.endDisplayList();
    ViewportContent content;
    content.images.push_back(
        std::make_shared<const z64::MemoryImage>(makeImage(toki::viewport::FrameSegment, writer)));
    content.draws.push_back(ViewportDraw{.image = 0, .displayList = start});

    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, OrbitCamera{}, FrameSize{});

    REQUIRE(frame.diagnostics.size() == 1);
    REQUIRE(frame.rspResult.outputs.empty());
}

TEST_CASE("each draw sees its own image in the segments they share", "[viewport]") {
    constexpr u8 Shared = 0x03;
    ViewportContent content;
    for (const base::s16 x : {base::s16{0}, base::s16{300}}) {
        n64::DisplayListWriter writer(Shared);
        const u32 vertices =
            writer.addVertex({.position = {x, 0, 0}, .color = {255, 255, 255, 255}});
        static_cast<void>(writer.addVertex(
            {.position = {static_cast<base::s16>(x + 50), 0, 0}, .color = {255, 255, 255, 255}}));
        static_cast<void>(
            writer.addVertex({.position = {x, 50, 0}, .color = {255, 255, 255, 255}}));
        const u32 start = writer.loadVertices(vertices, 3, 0);
        writer.drawTriangle({0, 1, 2});
        writer.endDisplayList();
        content.draws.push_back(ViewportDraw{.image = content.images.size(), .displayList = start});
        content.images.push_back(
            std::make_shared<const z64::MemoryImage>(makeImage(Shared, writer)));
    }
    REQUIRE(content.draws[0].displayList == content.draws[1].displayList);

    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, OrbitCamera{}, FrameSize{.width = 640, .height = 480});

    REQUIRE(frame.diagnostics.empty());
    const std::vector<n64::Triangle> triangles = findTriangles(frame.rspResult);
    REQUIRE(triangles.size() == 2);
    REQUIRE(triangles[0].vertices[0].position != triangles[1].vertices[0].position);
}

TEST_CASE("orbiting keeps the distance and stops short of the poles", "[viewport]") {
    OrbitCamera camera;
    const f32 distance = camera.getDistance();

    camera.rotate(10.0F, 500.0F);

    REQUIRE(glm::length(camera.getEye() - camera.getTarget()) == Catch::Approx(distance));
    REQUIRE(camera.getEye().y - camera.getTarget().y < distance);
    REQUIRE(camera.getEye().y - camera.getTarget().y > distance * 0.99F);
}

TEST_CASE("zooming in brings the camera closer and back out restores it", "[viewport]") {
    OrbitCamera camera;
    const f32 distance = camera.getDistance();

    camera.zoom(2.0F);
    REQUIRE(camera.getDistance() < distance);
    camera.zoom(-2.0F);

    REQUIRE(camera.getDistance() == Catch::Approx(distance));
}

TEST_CASE("a LookAt is the view's right and up, as guLookAtHilite makes it", "[viewport]") {
    const std::array<std::array<base::s8, 3>, 2> lookAt =
        toki::viewport::makeLookAt({0.0F, 0.0F, 100.0F}, {0.0F, 0.0F, 0.0F});

    REQUIRE(lookAt[0] == std::array<base::s8, 3>{-128, 0, 0});
    REQUIRE(lookAt[1] == std::array<base::s8, 3>{0, 127, 0});
}
