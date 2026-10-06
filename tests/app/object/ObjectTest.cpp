#include "app/object/domain/Object.h"

#include "app/object/domain/Animation.h"
#include "app/object/domain/ObjectAssets.h"
#include "app/object/domain/ObjectView.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "n64/DisplayListWriter.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using base::s16;
using base::u16;
using base::u32;
using base::u8;
using base::usize;

namespace {

constexpr u8 Segment = 0x06;

struct TestObject {
    z64::MemoryImage image;
    u32 displayList = 0;
    u32 skeleton = 0;
    u32 animation = 0;
};

/// Segment 6 of a small object, written by hand: a triangle's vertices and
/// its display list, three limbs that draw it, a flexible skeleton over them,
/// and a two-frame animation that turns the root.
class ObjectBuilder {
public:
    ObjectBuilder() {
        vertices = add("gTriVtx", "Vtx", true, [&] {
            static_cast<void>(
                writer.addVertex({.position = {0, 0, 0}, .color = {255, 255, 255, 255}}));
            static_cast<void>(
                writer.addVertex({.position = {100, 0, 0}, .color = {255, 255, 255, 255}}));
            static_cast<void>(
                writer.addVertex({.position = {0, 50, -20}, .color = {255, 255, 255, 255}}));
        });
        displayList = add("gTriDL", "Gfx", true, [&] {
            writer.loadVertices(vertices, 3, 0);
            writer.drawTriangle({0, 1, 2});
            writer.endDisplayList();
        });
        const std::array<std::array<s16, 3>, 3> joints{{{0, 10, 0}, {0, 0, 200}, {300, 0, 0}}};
        const std::array<std::array<u8, 2>, 3> links{{{1, 0xFF}, {0xFF, 2}, {0xFF, 0xFF}}};
        for (usize i = 0; i < joints.size(); ++i) {
            limbs[i] = add("gLimb" + std::to_string(i), "StandardLimb", false, [&] {
                addHalves({joints[i][0], joints[i][1], joints[i][2]});
                addBytes({links[i][0], links[i][1]});
                addWord(displayList);
            });
        }
        limbTable = add("gLimbs", "void*", true, [&] {
            for (const u32 limb : limbs) {
                addWord(limb);
            }
        });
        skeleton = add("gSkel", "FlexSkeletonHeader", false, [&] {
            addWord(limbTable);
            addBytes({3, 0, 0, 0, 3, 0, 0, 0});
        });
        frameData = add("gAnimFrameData", "s16", true, [&] {
            addHalves({0, 5, 0, 0x4000, 0x4000, 0x2000});
        });
        jointIndices = add("gAnimJointIndices", "JointIndex", true, [&] {
            addHalves({0, 1, 0, 2, 2, 4, 2, 2, 2, 2, 2, 2});
        });
        animation = add("gAnim", "AnimationHeader", false, [&] {
            addHalves({2, 0});
            addWord(frameData);
            addWord(jointIndices);
            addHalves({4, 0});
        });
    }

    [[nodiscard]] TestObject build() const {
        TestObject made{.image = image,
                        .displayList = displayList,
                        .skeleton = skeleton,
                        .animation = animation};
        made.image.segments.push_back(
            z64::ImageSegment{.number = Segment, .bytes = writer.getBytes()});
        return made;
    }

private:
    u32 vertices = 0;
    u32 displayList = 0;
    std::array<u32, 3> limbs{};
    u32 limbTable = 0;
    u32 skeleton = 0;
    u32 frameData = 0;
    u32 jointIndices = 0;
    u32 animation = 0;

    template <typename Write>
    u32 add(const std::string& name, const std::string& type, const bool isArray, Write write) {
        const u32 address = writer.getAddress();
        write();
        pending.resize((pending.size() + 7) / 8 * 8, 0);
        for (usize at = 0; at < pending.size(); at += 8) {
            writer.addCommand(readWord(at), readWord(at + 4));
        }
        pending.clear();
        image.symbols.emplace(name, z64::ImageSymbol{.segment = Segment,
                                                     .offset = address & 0x00FF'FFFFU,
                                                     .size = writer.getAddress() - address,
                                                     .type = type,
                                                     .isArray = isArray});
        return address;
    }

    [[nodiscard]] u32 readWord(const usize at) const {
        return (u32{pending[at]} << 24) | (u32{pending[at + 1]} << 16) |
               (u32{pending[at + 2]} << 8) | pending[at + 3];
    }

    void addHalves(const std::initializer_list<s16> halves) {
        for (const s16 half : halves) {
            pending.push_back(static_cast<u8>(static_cast<u16>(half) >> 8));
            pending.push_back(static_cast<u8>(half));
        }
    }

    void addBytes(const std::initializer_list<u8> values) {
        pending.insert(pending.end(), values);
    }

    void addWord(const u32 word) {
        pending.insert(pending.end(), {static_cast<u8>(word >> 24), static_cast<u8>(word >> 16),
                                       static_cast<u8>(word >> 8), static_cast<u8>(word)});
    }

    n64::DisplayListWriter writer{Segment};
    z64::MemoryImage image;
    std::vector<u8> pending;
};

toki::object::Object makeObject(const TestObject& test) {
    toki::object::Object object{.name = "object_test", .segment = Segment};
    object.images.push_back(std::make_shared<const z64::MemoryImage>());
    object.images.push_back(std::make_shared<const z64::MemoryImage>(test.image));
    object.objectImage = 1;
    object.assets = toki::object::listObjectAssets(*object.images[1], Segment);
    return object;
}

} // namespace

TEST_CASE("an object's assets are listed by the kind their types give them", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const std::vector<toki::object::ObjectAsset> assets =
        toki::object::listObjectAssets(test.image, Segment);

    const auto kindOf = [&](const std::string& name) {
        return std::ranges::find(assets, name, &toki::object::ObjectAsset::name)->kind;
    };
    REQUIRE(assets.size() == 10);
    REQUIRE(kindOf("gSkel") == toki::object::AssetKind::Skeleton);
    REQUIRE(kindOf("gAnim") == toki::object::AssetKind::Animation);
    REQUIRE(kindOf("gTriDL") == toki::object::AssetKind::DisplayList);
    REQUIRE(kindOf("gTriVtx") == toki::object::AssetKind::Vertices);
    REQUIRE(kindOf("gLimb1") == toki::object::AssetKind::Limb);
    REQUIRE(std::ranges::is_sorted(assets, {}, &toki::object::ObjectAsset::address));
}

TEST_CASE("a flexible skeleton reads its limbs through its limb table", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const toki::object::Skeleton skeleton = toki::object::readSkeleton(test.image, test.skeleton);

    REQUIRE(skeleton.diagnostics.empty());
    REQUIRE(skeleton.isFlexible);
    REQUIRE(skeleton.displayListCount == 3);
    REQUIRE(skeleton.limbs.size() == 3);
    REQUIRE(skeleton.limbs[1].jointPosition == std::array<s16, 3>{0, 0, 200});
    REQUIRE(skeleton.limbs[0].child == 1);
    REQUIRE(skeleton.limbs[1].sibling == 2);
    REQUIRE(skeleton.limbs[2].displayList == test.displayList);
}

TEST_CASE("an animation frame takes static values once and dynamic ones per frame", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const auto animation = toki::object::readAnimation(test.image, test.animation);

    REQUIRE(animation.has_value());
    REQUIRE(animation->frameCount == 2);
    REQUIRE(animation->jointIndices.size() == 4);
    const toki::object::JointTable first = toki::object::makeJointTable(*animation, 0);
    const toki::object::JointTable second = toki::object::makeJointTable(*animation, 1);
    REQUIRE(first[0] == std::array<s16, 3>{0, 5, 0});
    REQUIRE(first[1] == std::array<s16, 3>{0, 0, 0x4000});
    REQUIRE(second[1] == std::array<s16, 3>{0, 0, 0x2000});
    REQUIRE(second[2] == std::array<s16, 3>{0, 0, 0});
}

TEST_CASE("a pose turns each limb with its parent, children before siblings", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const toki::object::Skeleton skeleton = toki::object::readSkeleton(test.image, test.skeleton);
    toki::object::JointTable table = toki::object::makeRestJointTable(skeleton);
    table[1] = {0, 0x4000, 0};

    const toki::object::Pose pose = toki::object::poseSkeleton(skeleton, table);

    REQUIRE(pose.drawn.size() == 3);
    REQUIRE(pose.drawn[0].limb == 0);
    REQUIRE(pose.drawn[1].limb == 1);
    REQUIRE(pose.drawn[2].limb == 2);
    REQUIRE(pose.joints[0][1] == Catch::Approx(10.0F));
    REQUIRE(pose.joints[1][0] == Catch::Approx(200.0F).margin(0.01));
    REQUIRE(pose.joints[1][2] == Catch::Approx(0.0F).margin(0.01));
    REQUIRE(pose.joints[2][0] == Catch::Approx(0.0F).margin(0.01));
    REQUIRE(pose.joints[2][2] == Catch::Approx(-300.0F).margin(0.01));
}

TEST_CASE("a display list's bounds are every vertex it loads", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const auto bounds = toki::object::findDisplayListBounds(test.image, test.displayList);

    REQUIRE(bounds.has_value());
    REQUIRE(bounds->lowest == glm::vec3(0.0F, 0.0F, -20.0F));
    REQUIRE(bounds->highest == glm::vec3(100.0F, 50.0F, 0.0F));
}

TEST_CASE("an animated skeleton draws every limb through segment 13", "[object]") {
    const TestObject test = ObjectBuilder().build();
    const toki::object::Object object = makeObject(test);
    const toki::object::ObjectContent made = toki::object::makeObjectContent(
        object, {.skeleton = test.skeleton, .animation = test.animation, .frame = 1}, std::nullopt);

    REQUIRE(made.diagnostics.empty());
    REQUIRE(made.frameCount == 2);
    REQUIRE(made.bounds.has_value());
    const toki::viewport::Frame frame = toki::viewport::runFrame(
        made.content, toki::viewport::OrbitCamera{}, {.width = 320, .height = 240});
    REQUIRE(frame.diagnostics.empty());
    const auto triangles = std::ranges::count_if(frame.rspResult.outputs, [](const auto& output) {
        return std::holds_alternative<n64::Triangle>(output);
    });
    REQUIRE(triangles == 3);
}
