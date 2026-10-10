#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/object/domain/ObjectView.h"
#include "app/scene/domain/ActorDraws.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/OrbitCamera.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "gfx/Renderer.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"
#include "z64/MemoryImage.h"
#include "z64/PngImage.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

using base::f32;
using base::s16;
using base::s64;
using base::u16;
using base::u32;
using base::usize;

namespace {

constexpr u32 ImageSize = 256;

constexpr toki::viewport::ViewportLighting RenderLighting{
    .ambient = {0x60, 0x60, 0x60},
    .lights = {toki::viewport::ViewportLight{.direction = {0x49, 0x49, 0x49},
                                             .color = {0xC8, 0xC8, 0xC0}},
               toki::viewport::ViewportLight{.direction = {-0x49, -0x49, -0x49},
                                             .color = {0x40, 0x40, 0x48}}},
    .fogColor = {0, 0, 0},
    .fogNear = 1000,
    .zFar = toki::viewport::OrbitCamera::Far,
};

[[nodiscard]] std::string describe(const std::vector<base::Diagnostic>& diagnostics) {
    std::string described;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        described += base::describe(diagnostic) + "\n";
    }
    return described;
}

/// Sets `field`'s bits in `bits` to `value`.
void setField(u16& bits, const toki::actor::ParamField& field, const s64 value) {
    const u32 mask = ((1U << field.width) - 1U) << field.shift;
    bits = static_cast<u16>((bits & ~mask) | ((static_cast<u32>(value) << field.shift) & mask));
}

/// Params, rotation and age that `variant`'s match holds for.
[[nodiscard]] toki::actor::PlacementValues makeValues(const toki::actor::ActorEntry& entry,
                                                      const toki::actor::ActorVariant& variant) {
    toki::actor::PlacementValues values{.params = variant.defaultParams.value_or(0),
                                        .age = toki::actor::Age::Child};
    for (const toki::actor::FieldMatch& match : variant.match) {
        const toki::actor::MatchValue& first = match.values.front();
        if (match.field == "age") {
            values.age = std::get<std::string>(first) == "adult" ? toki::actor::Age::Adult
                                                                 : toki::actor::Age::Child;
            continue;
        }
        if (match.field == "scene") {
            values.scene = std::get<std::string>(first);
            continue;
        }
        const auto field =
            std::ranges::find(entry.fields, match.field, &toki::actor::ParamField::name);
        REQUIRE(field != entry.fields.end());
        s64 value = 0;
        if (const bool* flag = std::get_if<bool>(&first)) {
            value = *flag ? 1 : 0;
        } else if (const s64* number = std::get_if<s64>(&first)) {
            value = *number;
        } else {
            const auto named = std::ranges::find(field->values, std::get<std::string>(first),
                                                 &toki::actor::EnumValue::name);
            REQUIRE(named != field->values.end());
            value = named->value;
        }
        if (field->source == toki::actor::FieldSource::Params) {
            setField(values.params, *field, value);
        } else {
            const usize axis = static_cast<usize>(field->source) - 1;
            u16 rotation = static_cast<u16>(values.rotation[axis]);
            setField(rotation, *field, value);
            values.rotation[axis] = static_cast<s16>(rotation);
        }
    }
    return values;
}

struct Box {
    glm::vec3 lowest{1e9F};
    glm::vec3 highest{-1e9F};

    void add(const toki::object::Bounds& bounds, const glm::mat4& matrix) {
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 point((corner & 1) != 0 ? bounds.highest.x : bounds.lowest.x,
                                  (corner & 2) != 0 ? bounds.highest.y : bounds.lowest.y,
                                  (corner & 4) != 0 ? bounds.highest.z : bounds.lowest.z);
            const glm::vec3 moved(matrix * glm::vec4(point, 1.0F));
            lowest = glm::min(lowest, moved);
            highest = glm::max(highest, moved);
        }
    }
};

[[nodiscard]] Box measure(const toki::scene::SceneActors& actors,
                          const toki::scene::ActorDrawing& drawing,
                          const toki::scene::PlacedActor& placed) {
    Box box;
    const glm::mat4 matrix =
        toki::scene::makeActorMatrix(placed, drawing.scale, drawing.heightOffset, drawing.rotates);
    const auto addList = [&](const toki::scene::ActorDisplayList& list, const glm::mat4& parent) {
        const z64::MemoryImage& image = *actors.objects[list.object].image;
        if (const auto bounds = toki::object::findDisplayListBounds(image, list.address)) {
            box.add(*bounds, parent * list.state.placement);
        }
    };
    if (drawing.skeleton) {
        const toki::scene::SkeletonDrawing& skeleton = *drawing.skeleton;
        const z64::MemoryImage& image = *actors.objects[skeleton.object].image;
        const glm::mat4 placedMatrix = matrix * skeleton.state.placement;
        for (const toki::object::PosedLimb& limb : skeleton.pose.drawn) {
            if (const auto bounds = toki::object::findDisplayListBounds(image, limb.displayList)) {
                box.add(*bounds, placedMatrix * limb.matrix);
            }
        }
    }
    for (const toki::scene::ActorDisplayList& list : drawing.displayLists) {
        addList(list, matrix);
    }
    for (const toki::scene::ChildPlacement& child : drawing.children) {
        const auto model = actors.models.find(child.id);
        const toki::scene::ActorDrawing* childDrawing =
            model != actors.models.end() ? model->second.findDrawing({.params = child.params})
                                         : nullptr;
        if (childDrawing == nullptr) {
            continue;
        }
        const toki::scene::PlacedActor childPlaced{.id = child.id,
                                                   .position = {static_cast<s16>(child.offset[0]),
                                                                static_cast<s16>(child.offset[1]),
                                                                static_cast<s16>(child.offset[2])},
                                                   .params = child.params};
        const Box childBox = measure(actors, *childDrawing, childPlaced);
        box.lowest = glm::min(box.lowest, childBox.lowest);
        box.highest = glm::max(box.highest, childBox.highest);
    }
    if (box.lowest.x > box.highest.x) {
        box.lowest = glm::vec3(-20.0F);
        box.highest = glm::vec3(20.0F);
    }
    return box;
}

struct Harness {
    std::shared_ptr<const hackeroot::Workspace> workspace;
    hackeroot::AssetImageReader reader;
    std::shared_ptr<const z64::MemoryImage> code;
    gfx::Device& device;
    gfx::Renderer& renderer;
    gfx::RenderTarget& target;
    std::filesystem::path output;

    [[nodiscard]] std::optional<s16> findActorId(const std::string& enumName) const {
        const auto& table = workspace->getTables().actors;
        const auto found = std::ranges::find(table, enumName, &hackeroot::ActorEntry::enumName);
        return found == table.end() ? std::nullopt : std::optional(static_cast<s16>(found->id));
    }

    [[nodiscard]] s16 findKeepId(const toki::actor::ActorEntry& entry,
                                 const toki::actor::ActorVariant& variant) const {
        const std::optional<std::string>& object = variant.object ? variant.object : entry.object;
        if (!object || (*object != "gameplay_field_keep" && *object != "gameplay_dangeon_keep")) {
            return 0;
        }
        const auto& table = workspace->getTables().objects;
        const auto found = std::ranges::find(table, *object, &hackeroot::ObjectEntry::name);
        return found == table.end() ? s16{0} : static_cast<s16>(found->id);
    }

    void render(const toki::actor::ActorDatabase& shipped, const toki::actor::ActorEntry& entry,
                usize index);
};

void Harness::render(const toki::actor::ActorDatabase& shipped,
                     const toki::actor::ActorEntry& entry, const usize index) {
    const toki::actor::ActorVariant& variant = entry.variants[index];
    const std::optional<s16> id = findActorId(entry.actor);
    REQUIRE(id.has_value());
    const toki::actor::PlacementValues values = makeValues(entry, variant);
    toki::scene::SceneLayer layer{.index = values.age == toki::actor::Age::Adult ? 2U : 0U};
    layer.header.keepObjectId = findKeepId(entry, variant);
    layer.rooms.push_back(toki::scene::RoomHeader{
        .actors = {toki::scene::ActorPlacement{
            .id = *id, .rotation = values.rotation, .params = static_cast<s16>(values.params)}}});
    std::string folder;
    if (values.scene) {
        const auto& scenes = reader.getWorkspace().getTables().scenes;
        const auto scene =
            std::ranges::find(scenes, *values.scene, &hackeroot::SceneEntry::enumName);
        REQUIRE(scene != scenes.end());
        folder = scene->name.substr(0, scene->name.size() - std::string_view("_scene").size());
    }
    toki::scene::SceneActors actors =
        toki::scene::readSceneActors(reader, folder, {layer}, {.shipped = shipped});
    const toki::scene::SceneActorModel& model = actors.models.at(*id);
    INFO(entry.actor << " variant " << index << ":\n" << describe(model.diagnostics));
    CHECK(model.diagnostics.empty());
    const toki::scene::PlacedActor placed{
        .id = *id, .rotation = values.rotation, .params = values.params, .age = values.age};
    REQUIRE(index < model.variants.size());
    toki::scene::SceneActorModel& only = actors.models.at(*id);
    only.entry->variants = {variant};
    only.entry->variants.front().match.clear();
    only.variants = {model.variants[index]};
    const toki::scene::ActorDrawing* drawing = &only.variants.front();
    if (!drawing->isDrawn()) {
        return;
    }

    const hackeroot::GameSetup setup = hackeroot::findGameSetup(*code);
    toki::viewport::ViewportContent content{.images = {code}, .lighting = RenderLighting};
    const std::array<toki::scene::PlacedActor, 1> placements{placed};
    toki::scene::ActorDraws draws =
        toki::scene::makeActorDraws(actors, placements, 1, toki::scene::makeActorSetup(*code));
    std::ranges::move(draws.images, std::back_inserter(content.images));
    content.draws.push_back({.image = 0, .displayList = *setup.drawConfig});
    std::ranges::move(draws.opaque, std::back_inserter(content.draws));
    content.draws.push_back({.image = 0, .displayList = *setup.drawConfig});
    std::ranges::move(draws.translucent, std::back_inserter(content.draws));

    const Box box = measure(actors, *drawing, placed);
    toki::viewport::OrbitCamera camera;
    camera.setTarget((box.lowest + box.highest) * 0.5F);
    camera.setDistance(std::max(glm::length(box.highest - box.lowest) * 1.2F, 30.0F));
    camera.setAngles(35.0F, 20.0F);
    const toki::viewport::Frame frame =
        toki::viewport::runFrame(content, camera, {.width = ImageSize, .height = ImageSize});
    INFO("frame:\n" << describe(frame.diagnostics));
    CHECK(frame.diagnostics.empty());
    const std::vector<base::Diagnostic> drawn = renderer.render(
        frame.rspResult, frame.rdpResult, target, {.width = ImageSize, .height = ImageSize});
    INFO("renderer:\n" << describe(drawn));
    CHECK(drawn.empty());
    auto pixels = target.readPixels(device);
    REQUIRE(pixels.has_value());
    for (usize alpha = 3; alpha < pixels->size(); alpha += 4) {
        (*pixels)[alpha] = 0xFF;
    }
    const auto png = z64::encodePng(*pixels, ImageSize, ImageSize);
    REQUIRE(png.has_value());
    std::ofstream(output / std::format("{}_{}.png", entry.actor, index), std::ios::binary)
        .write(reinterpret_cast<const char*>(png->data()),
               static_cast<std::streamsize>(png->size()));
}

} // namespace

TEST_CASE("every variant of the shipped actor database is drawn to an image", "[.render]") {
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
        gfx::RenderTarget::create(*device, ImageSize, ImageSize);
    REQUIRE(target.has_value());
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
        hackeroot::Workspace::load(repository, {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    hackeroot::AssetImageReader reader(workspace);
    const std::array code{hackeroot::makeSharedGameCode(workspace->getRoot())};
    const std::shared_ptr<const z64::MemoryImage> codeImage = reader.read(code, {}).shared.front();

    const toki::actor::ActorDatabase shipped = toki::actor::readActorDatabase(TOKI_SHIPPED_ACTORS);
    REQUIRE(shipped.diagnostics.empty());
    const std::filesystem::path output = std::filesystem::path(TOKI_GPU_OUTPUT) / "actor-renders";
    std::error_code failure;
    std::filesystem::create_directories(output, failure);
    Harness harness{.workspace = workspace,
                    .reader = std::move(reader),
                    .code = codeImage,
                    .device = *device,
                    .renderer = *renderer,
                    .target = *target,
                    .output = output};
    for (const auto& [name, entry] : shipped.entries) {
        for (usize index = 0; index < entry.variants.size(); ++index) {
            harness.render(shipped, entry, index);
        }
    }
}
