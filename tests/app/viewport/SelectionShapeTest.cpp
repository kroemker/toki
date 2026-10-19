#include "app/viewport/domain/SelectionShape.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

using base::f32;
using base::usize;
using toki::viewport::DrawnOwners;
using toki::viewport::OutputRange;

namespace {

n64::Triangle makeTriangle(const f32 x) {
    n64::Triangle triangle;
    triangle.vertices[0].position = {x, 0.0F, 0.0F, 1.0F};
    triangle.vertices[1].position = {x + 1.0F, 0.0F, 0.5F, 1.0F};
    triangle.vertices[2].position = {x, 2.0F, 0.0F, 1.0F};
    return triangle;
}

} // namespace

TEST_CASE("what is selected is what its own draws drew, boxed in the world",
          "[viewport][selection]") {
    n64::RspResult result;
    result.outputs.emplace_back(makeTriangle(0.0F));
    result.outputs.emplace_back(n64::RdpCommand{});
    result.outputs.emplace_back(makeTriangle(5.0F));
    result.outputs.emplace_back(makeTriangle(9.0F));
    const std::array<std::optional<OutputRange>, 3> ranges{OutputRange{.first = 0, .end = 2},
                                                           OutputRange{.first = 2, .end = 3},
                                                           OutputRange{.first = 3, .end = 4}};
    const std::array<std::optional<usize>, 3> owners{usize{7}, usize{8}, usize{7}};
    const DrawnOwners drawn{.ranges = ranges, .owners = owners};
    const std::array<usize, 1> selected{7};

    REQUIRE(toki::viewport::listOwnedTriangles(result, drawn, selected).size() == 2);

    const auto boxes = toki::viewport::findOwnedBoxes(result, drawn, selected, glm::mat4(1.0F));
    REQUIRE(boxes.size() == 1);
    REQUIRE(boxes[0]->lowest == glm::vec3(0.0F, 0.0F, 0.0F));
    REQUIRE(boxes[0]->highest == glm::vec3(10.0F, 2.0F, 0.5F));

    const std::array<usize, 1> nothing{3};
    REQUIRE_FALSE(toki::viewport::findOwnedBoxes(result, drawn, nothing, glm::mat4(1.0F))[0]);
}

TEST_CASE("a box has twelve edges, each along one axis", "[viewport][selection]") {
    const auto edges =
        toki::viewport::listBoxEdges({.lowest = {0.0F, 0.0F, 0.0F}, .highest = {1.0F, 2.0F, 3.0F}});
    for (const auto& [start, end] : edges) {
        const glm::vec3 along = end - start;
        const int axes =
            (along.x != 0.0F ? 1 : 0) + (along.y != 0.0F ? 1 : 0) + (along.z != 0.0F ? 1 : 0);
        REQUIRE(axes == 1);
    }
}
