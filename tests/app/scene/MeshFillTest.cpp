#include "app/scene/domain/MeshFill.h"

#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MeshDraft.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <expected>
#include <string>
#include <vector>

using base::usize;
using toki::scene::DraftFace;
using toki::scene::RoomDraft;

namespace {

/// The corners of a square on the ground, 100 wide, around from the origin,
/// and one more above the far corner, off its plane.
RoomDraft makeSquare(const std::vector<std::array<usize, 3>>& faces) {
    RoomDraft draft;
    for (const glm::vec3& position :
         {glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(100.0F, 0.0F, 0.0F),
          glm::vec3(100.0F, 0.0F, 100.0F), glm::vec3(0.0F, 0.0F, 100.0F),
          glm::vec3(100.0F, 50.0F, 100.0F)}) {
        draft.vertices.push_back({.position = position});
    }
    for (const std::array<usize, 3>& corners : faces) {
        DraftFace face{.collision = toki::scene::CollisionMaterial{.type = 4}, .original = 0};
        for (usize corner = 0; corner < 3; ++corner) {
            face.corners[corner].vertex = corners[corner];
            toki::scene::setVtxPosition(face.corners[corner].vtx, glm::mat4(1.0F),
                                        draft.vertices[corners[corner]].position);
        }
        draft.faces.push_back(face);
    }
    return draft;
}

glm::vec3 findNormal(const RoomDraft& draft, const DraftFace& face) {
    const glm::vec3 first = toki::scene::findCornerPosition(draft, face, 0);
    return glm::cross(toki::scene::findCornerPosition(draft, face, 1) - first,
                      toki::scene::findCornerPosition(draft, face, 2) - first);
}

} // namespace

TEST_CASE("vertices picked in any order are made faces around them, toward the viewer",
          "[scene][mesh]") {
    RoomDraft draft = makeSquare({{0, 4, 2}});
    const std::array<usize, 4> picked{2, 0, 3, 1};

    const std::expected<std::vector<usize>, std::string> made =
        toki::scene::fillVertices(draft, picked, {50.0F, 500.0F, 50.0F});

    REQUIRE(made.has_value());
    REQUIRE(made->size() == 2);
    for (const usize face : *made) {
        CHECK(findNormal(draft, draft.faces[face]).y > 0.0F);
        CHECK_FALSE(draft.faces[face].original.has_value());
        CHECK(draft.faces[face].collision == toki::scene::CollisionMaterial{.type = 4});
        for (usize corner = 0; corner < 3; ++corner) {
            CHECK(toki::scene::findCornerPosition(draft, draft.faces[face], corner) ==
                  draft.vertices[draft.faces[face].corners[corner].vertex].position);
        }
    }

    RoomDraft below = makeSquare({{0, 4, 2}});
    const std::expected<std::vector<usize>, std::string> turned =
        toki::scene::fillVertices(below, picked, {50.0F, -500.0F, 50.0F});
    REQUIRE(turned.has_value());
    CHECK(findNormal(below, below.faces[turned->front()]).y < 0.0F);
}

TEST_CASE("a face made beside another runs its shared edge the other way", "[scene][mesh]") {
    RoomDraft draft = makeSquare({{0, 3, 2}});
    const std::array<usize, 3> picked{0, 1, 2};

    const std::expected<std::vector<usize>, std::string> made =
        toki::scene::fillVertices(draft, picked, {50.0F, -500.0F, 50.0F});

    REQUIRE(made.has_value());
    REQUIRE(made->size() == 1);
    CHECK(findNormal(draft, draft.faces[made->front()]).y > 0.0F);
}

TEST_CASE("vertices off one plane make faces at their own corners", "[scene][mesh]") {
    RoomDraft draft = makeSquare({{0, 3, 2}});
    const std::array<usize, 4> picked{0, 1, 4, 3};

    const std::expected<std::vector<usize>, std::string> made =
        toki::scene::fillVertices(draft, picked, {50.0F, 500.0F, 50.0F});

    REQUIRE(made.has_value());
    REQUIRE(made->size() == 2);
    bool usesRaised = false;
    for (const usize face : *made) {
        for (const toki::scene::DraftCorner& corner : draft.faces[face].corners) {
            usesRaised = usesRaised || corner.vertex == 4;
        }
    }
    CHECK(usesRaised);
}

TEST_CASE("vertices in a line, or fewer than three, make no face", "[scene][mesh]") {
    RoomDraft draft = makeSquare({{0, 3, 2}});
    draft.vertices.push_back({.position = {50.0F, 0.0F, 0.0F}});
    const std::array<usize, 3> inLine{0, 5, 1};
    const std::array<usize, 2> two{0, 1};

    CHECK_FALSE(toki::scene::fillVertices(draft, inLine, {0.0F, 500.0F, 0.0F}).has_value());
    CHECK_FALSE(toki::scene::fillVertices(draft, two, {0.0F, 500.0F, 0.0F}).has_value());
    CHECK(draft.faces.size() == 1);
}
