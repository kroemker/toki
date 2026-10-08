#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace toki::scene {

/// An actor where a scene places it: by ID, position, rotation in binary
/// angles and params, and Link's age in the layer it is placed in, which its
/// variant is picked by.
struct PlacedActor {
    s16 id = 0;
    std::array<s16, 3> position{};
    std::array<s16, 3> rotation{};
    u16 params = 0;
    std::optional<actor::Age> age;
};

/// What drawing placed actors as their models adds to a scene's content: the
/// actor objects and one image per drawn actor, and the draws of each pass.
/// Draws name images by their index in the content, the first of these at
/// `firstImage`.
struct ActorDraws {
    std::vector<std::shared_ptr<const z64::MemoryImage>> images;
    std::vector<viewport::ViewportDraw> opaque;
    std::vector<viewport::ViewportDraw> translucent;
    /// The placed actors without a drawing, by index, to be marked instead.
    std::vector<usize> undrawn;
};

/// The matrix `Actor_Draw` draws an actor with: translated to its position,
/// raised by its height offset times its scale, turned about Y, then X, then Z
/// on the axes it turns by, and scaled.
[[nodiscard]] glm::mat4 makeActorMatrix(const PlacedActor& actor, f32 scale, f32 yOffset,
                                        const actor::RotationAxes& rotates = {});

/// What actors are drawn after, from the game code, which is the content's
/// first image: `sSetupDL` by index, which a model is drawn after
/// `SETUPDL_25` of unless its steps say another, and a shadow after
/// `SETUPDL_44` of; and `gEmptyDL` for segments 8 to 12 where an actor's
/// `Draw` points them at nothing that could be read.
struct ActorSetup {
    std::vector<u32> setupDisplayLists;
    std::optional<u32> emptyDisplayList;
    /// The frame animated actors are posed on, counted from when they started
    /// playing; nothing to leave each on its variant's frame.
    std::optional<usize> frame;
};

/// The setup of `code`, an image of `hackeroot::findGameCodeFiles`, for
/// actors posed on `frame`.
[[nodiscard]] ActorSetup makeActorSetup(const z64::MemoryImage& code,
                                        std::optional<usize> frame = std::nullopt);

/// Draws every placed actor whose variant draws as `Actor_Draw` would: each
/// skeleton and display list after its setup display list, with segment 6 or
/// whichever its object is in pointed at the object and segments 8 to 12
/// where its steps point them, in its colours, by the actor's matrix and its
/// own placement; a skeleton's limbs by their matrices, laid out in segment 13
/// for a flexible skeleton's display lists to find, and the display lists
/// drawn with a limb by its matrix; display lists facing the camera when they
/// are billboards; its shadow, as `ActorShadow_Draw` does, on the ground where it
/// stands; and the children it spawns, by their own entries. Every actor's list
/// leaves the modelview at the identity, as the rooms drawn after it expect.
[[nodiscard]] ActorDraws makeActorDraws(const SceneActors& actors,
                                        std::span<const PlacedActor> placed, usize firstImage,
                                        const ActorSetup& setup);

} // namespace toki::scene
