#pragma once

#include "app/actor/domain/ActorModel.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "z64/MemoryImage.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

struct SceneLayer;

/// An object read for a scene's actors, in the segment the game loads it into:
/// 4 for `gameplay_keep`, 5 for the scene's keep object, 6 for the rest.
struct ActorObject {
    std::string name;
    u8 segment = 0;
    std::shared_ptr<const z64::MemoryImage> image;
};

/// A segment an actor points at an address in one of the actor objects before
/// it draws, as at its eye texture.
struct ActorSegment {
    u8 segment = 0;
    actor::Pass pass = actor::Pass::Opaque;
    usize object = 0;
    u32 address = 0;
};

/// A display list an actor draws, by address in one of the actor objects, and
/// the segments it is drawn with.
struct ActorDisplayList {
    usize object = 0;
    u32 address = 0;
    actor::Pass pass = actor::Pass::Opaque;
    std::vector<ActorSegment> segments;
};

/// A skeleton and the animation it plays, kept to pose it on any frame.
struct ActorAnimation {
    object::Skeleton skeleton;
    object::Animation animation;
};

/// What an actor draws, made ready when the scene is read: a skeleton posed on
/// the first frame of its animation, or at rest, with every limb's matrix from
/// model space, or else its display lists.
struct ActorDrawing {
    usize skeletonObject = 0;
    std::vector<object::PosedLimb> limbs;
    std::optional<ActorAnimation> animation;
    std::vector<ActorDisplayList> displayLists;
    /// The segments a skeleton is drawn with.
    std::vector<ActorSegment> segments;
};

/// One actor of the scene: how its C says it is drawn, and what of that could
/// be found in its objects. An actor without a drawing is marked instead, and
/// `diagnostics` says why when its model was read but could not be drawn.
struct SceneActorModel {
    actor::ActorModel model;
    std::optional<ActorDrawing> drawing;
    std::vector<base::Diagnostic> diagnostics;
};

/// The models of every actor a scene places, in any layer, by actor ID, and the
/// objects they are drawn from.
struct SceneActors {
    std::map<s16, SceneActorModel> models;
    std::vector<ActorObject> objects;
    /// The overlays and objects read, for reading the scene again when one changes.
    std::vector<std::filesystem::path> watched;
    std::chrono::milliseconds elapsed{};
};

/// Reads the model of every actor `layers` place, the objects those models
/// name, and poses every skeleton found. The scene keep object, when not zero,
/// is read into segment 5, as `SCENE_CMD_SPECIAL_FILES` has the game load it.
/// Whether any actor of `actors` plays an animation.
[[nodiscard]] bool hasAnimatedActors(const SceneActors& actors);

[[nodiscard]] SceneActors readSceneActors(hackeroot::AssetImageReader& reader,
                                          const std::vector<SceneLayer>& layers);

} // namespace toki::scene
