#pragma once

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

/// An actor where a scene places it: by ID, position and rotation in binary
/// angles.
struct PlacedActor {
    s16 id = 0;
    std::array<s16, 3> position{};
    std::array<s16, 3> rotation{};
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
/// raised by its height offset times its scale, turned about Y, then X, then Z,
/// and scaled.
[[nodiscard]] glm::mat4 makeActorMatrix(const PlacedActor& actor, f32 scale, f32 yOffset);

/// What actors are drawn after, from the game code, which is the content's
/// first image: `sSetupDL[SETUPDL_25]`, and `gEmptyDL` for segments 8 to 12
/// where an actor's `Draw` points them at nothing that could be read.
struct ActorSetup {
    std::optional<u32> setupDisplayList;
    std::optional<u32> emptyDisplayList;
};

/// Draws every placed actor that has a drawing as `Actor_Draw` would: each
/// after the setup display list, with segment 6 or whichever its object is in
/// pointed at the object and segments 8 to 12 where its `Draw` points them, a
/// skeleton's limbs by their matrices, laid out in segment 13 for a flexible
/// skeleton's display lists to find, and display lists by the actor's matrix.
/// Every actor's list leaves the modelview at the identity, as the rooms drawn
/// after it expect.
[[nodiscard]] ActorDraws makeActorDraws(const SceneActors& actors,
                                        std::span<const PlacedActor> placed, usize firstImage,
                                        const ActorSetup& setup);

} // namespace toki::scene
