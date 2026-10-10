#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Pose.h"
#include "app/object/domain/Skeleton.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "z64/MemoryImage.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <array>
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
    usize object = 0;
    u32 address = 0;
};

/// What a display list or skeleton is drawn under, as its part says: after
/// `sSetupDL[setup]`, in its colours, with a `LookAt` toward the actor when it
/// is hilit, with segments 8 to 12 pointed at its symbols, placed relative to
/// the actor, and facing the camera when it is a billboard.
struct DrawState {
    u8 setup = 25;
    actor::DrawColors colors;
    bool isHilit = false;
    std::vector<ActorSegment> segments;
    glm::mat4 placement{1.0F};
    bool isBillboard = false;
};

/// A display list an actor draws, by address in one of the actor objects, in
/// a pass and under a state.
struct ActorDisplayList {
    usize object = 0;
    u32 address = 0;
    actor::Pass pass = actor::Pass::Opaque;
    DrawState state;
};

/// A skeleton posed on the first frame of its animation, or at rest, with
/// every limb's matrix from model space, and the state it is drawn under.
struct SkeletonDrawing {
    usize object = 0;
    object::Pose pose;
    actor::Pass pass = actor::Pass::Opaque;
    DrawState state;
};

/// An actor a variant spawns, by ID, drawn by its own entry.
struct ChildPlacement {
    s16 id = 0;
    u16 params = 0;
    std::array<f32, 3> offset{};
};

/// What one variant of an actor draws, made ready when the scene is read.
struct ActorDrawing {
    actor::DrawKind kind = actor::DrawKind::Model;
    std::vector<SkeletonDrawing> skeletons;
    std::vector<ActorDisplayList> displayLists;
    f32 scale = actor::ActorModel::DefaultScale;
    f32 heightOffset = 0.0F;
    actor::RotationAxes rotates;
    std::vector<ChildPlacement> children;

    /// Whether anything is drawn for it: a model, or children in its place.
    [[nodiscard]] bool isDrawn() const {
        return kind != actor::DrawKind::Marker &&
               (!skeletons.empty() || !displayLists.empty() || !children.empty());
    }
};

/// Where an actor's entry came from.
enum class ModelSource : u8 {
    Shipped,
    Checkout,
    Guessed,
    None,
};

/// One actor of the scene: the entry it is drawn by, from a database or
/// guessed from its C, and what each of the entry's variants draws, as far as
/// its objects have what the entry names. `diagnostics` says what they do not.
struct SceneActorModel {
    std::string enumName;
    std::optional<actor::ActorEntry> entry;
    ModelSource source = ModelSource::None;
    /// What the actor's C says, when the entry was guessed from it.
    std::optional<actor::ActorModel> guess;
    /// One drawing for each of the entry's variants, in order.
    std::vector<ActorDrawing> variants;
    std::vector<base::Diagnostic> diagnostics;

    /// Whether any variant draws.
    [[nodiscard]] bool hasDrawing() const;

    /// The drawing of the variant `placement` matches, if one matches.
    [[nodiscard]] const ActorDrawing* findDrawing(const actor::PlacementValues& placement) const;
};

/// The shipped actor database and the checkout's, as one placement's entry is
/// looked up in.
struct ActorDatabases {
    actor::ActorDatabase shipped;
    actor::ActorDatabase checkout;
};

/// The models of every actor a scene places, in any layer, by actor ID, and the
/// objects they are drawn from.
struct SceneActors {
    std::map<s16, SceneActorModel> models;
    /// The scene's enum name, `SCENE_DEKU_TREE`, which variants may match on.
    std::optional<std::string> scene;
    std::vector<ActorObject> objects;
    /// The overlays and objects read, for reading the actors again when one changes.
    std::vector<std::filesystem::path> watched;
    std::chrono::milliseconds elapsed{};
    /// Of `elapsed`, the time spent reading overlays, and reading objects;
    /// the rest is posing and finding symbols.
    std::chrono::milliseconds overlaysElapsed{};
    std::chrono::milliseconds objectsElapsed{};
    /// What was wrong with the actor databases the actors were read by.
    std::vector<base::Diagnostic> diagnostics;
};

/// Link's age in a scene layer: child in the first two, adult in the next two,
/// none in a cutscene layer.
[[nodiscard]] std::optional<actor::Age> findLayerAge(usize layerIndex);

/// Reads the model of every actor `layers` place, player entries and the
/// children their variants spawn included: each by its entry in `databases`,
/// or else guessed from its overlay, with the objects its entry names and
/// every skeleton posed. `gameplay_keep` is always read, and the scene keep
/// object, when not zero, into segment 5, as `SCENE_CMD_SPECIAL_FILES` has the
/// game load it: every object's display lists may point into either.
/// `sceneFolder` names the scene's folder, `ydan`, whose enum name the scene
/// table gives; a variant that matches on other scenes is not read.
[[nodiscard]] SceneActors readSceneActors(hackeroot::AssetImageReader& reader,
                                          std::string_view sceneFolder,
                                          const std::vector<SceneLayer>& layers,
                                          const ActorDatabases& databases);

/// The values a placement's variant is found by.
[[nodiscard]] actor::PlacementValues makePlacementValues(const SceneActors& actors, u16 params,
                                                         const std::array<s16, 3>& rotation,
                                                         std::optional<actor::Age> age);

} // namespace toki::scene
