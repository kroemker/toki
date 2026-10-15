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
#include <variant>
#include <vector>

namespace toki::scene {

struct SceneLayer;

/// An object read for a scene's actors, in the segment the game loads it into:
/// 4 for `gameplay_keep`, 5 for the scene's keep object, 6 for the rest.
struct ActorObject {
    std::string name;
    u8 segment = 0;
    std::shared_ptr<const z64::MemoryImage> image;
    /// The other objects it points into, each in the segment the spec links it
    /// at, as `object_mori_objects` points into `object_mori_tex` in 8.
    std::vector<std::shared_ptr<const z64::MemoryImage>> linked;
};

/// An address in one of the actor objects, as of an eye texture.
struct ObjectAddress {
    usize object = 0;
    u32 address = 0;
};

/// An address in the game's code, as of `&D_80116280[2]`.
struct CodeAddress {
    u32 address = 0;
};

/// A segment an actor points before it draws: at an address in one of the
/// actor objects, at a list it builds, as its colours, at matrices it
/// computes, or into the game's code.
struct ActorSegment {
    u8 segment = 0;
    std::variant<ObjectAddress, actor::BuiltList, actor::SegmentMatrices, CodeAddress> target;
    /// The render mode a built list sets, its cycles evaluated as `gbi.h`
    /// defines them.
    std::optional<u32> renderMode;
};

/// What a display list or skeleton is drawn under, as its part says: after
/// `sSetupDL[setup]`, in its colours, with a `LookAt` toward the actor when it
/// is hilit, with segments 8 to 12 pointed as it says, placed relative to
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
    /// The variant's part it is, counted from 0, for the adjustments that name it.
    usize part = 0;
    usize object = 0;
    u32 address = 0;
    actor::Pass pass = actor::Pass::Opaque;
    DrawState state;
};

/// A limb a skeleton draws from another object than its own, as a Kokiri's
/// head: its place among the pose's drawn limbs, and the object.
struct LimbObject {
    usize drawn = 0;
    usize object = 0;
};

/// A skeleton posed on the first frame of its animation, or at rest, with
/// every limb's matrix from model space, and the state it is drawn under.
struct SkeletonDrawing {
    /// The variant's part it is, counted from 0, for the adjustments that name it.
    usize part = 0;
    usize object = 0;
    /// Posed, with its part's limbs swapped in; a limb drawn as nothing keeps
    /// its place, display list zero, for the matrices a flexible skeleton's
    /// display lists find in segment 13.
    object::Pose pose;
    std::vector<LimbObject> limbObjects;
    actor::Pass pass = actor::Pass::Opaque;
    DrawState state;
};

/// A param a child takes from its parent's: the parent's param, and the
/// child's it is written into.
struct TakenParam {
    actor::ActorParam from;
    actor::ActorParam to;
};

/// An actor a variant spawns, by ID, drawn by its own entry, with the params
/// it takes from the parent's placement written over its own.
struct ChildPlacement {
    s16 id = 0;
    u16 params = 0;
    std::array<f32, 3> offset{};
    std::vector<TakenParam> takes;
};

/// What an item part picked by a param draws, for each value of the param it
/// can draw: the param, and the display lists of each value's item.
struct ItemLists {
    actor::ActorParam param;
    std::map<s64, std::vector<ActorDisplayList>> byValue;
};

/// What one variant of an actor draws, made ready when the scene is read. Its
/// items picked by params are drawn by `SceneActorModel::makeDrawing`, which
/// knows the params.
struct ActorDrawing {
    actor::DrawKind kind = actor::DrawKind::Model;
    std::vector<SkeletonDrawing> skeletons;
    std::vector<ActorDisplayList> displayLists;
    f32 scale = actor::ActorModel::DefaultScale;
    f32 heightOffset = 0.0F;
    actor::RotationAxes rotates;
    std::vector<ChildPlacement> children;
    std::vector<ItemLists> items;

    /// Whether anything is drawn for it: a model, or children in its place.
    [[nodiscard]] bool isDrawn() const {
        return kind != actor::DrawKind::Marker &&
               (!skeletons.empty() || !displayLists.empty() || !children.empty() || !items.empty());
    }
};

/// One adjustment of an entry, made ready when the scene is read: what it
/// changes, its segments' symbols found in the entry's object.
struct AdjustmentDrawing {
    std::optional<f32> scale;
    std::optional<f32> heightOffset;
    std::optional<actor::RotationAxes> rotates;
    actor::DrawColors colors;
    std::vector<ActorSegment> segments;
    std::vector<usize> parts;
};

/// Makes `adjustment` to `drawing`: its scale, height offset and axes, and the
/// colours and segments of the parts it names, or of every part.
void applyAdjustment(ActorDrawing& drawing, const AdjustmentDrawing& adjustment);

/// One actor of the scene: the entry it is drawn by, when the actor database
/// has one, and what each of the entry's variants draws and each of its
/// adjustments changes, as far as its objects have what the entry names.
/// `diagnostics` says what they do not.
struct SceneActorModel {
    std::string enumName;
    std::optional<actor::ActorEntry> entry;
    /// One drawing for each of the entry's variants, in order.
    std::vector<ActorDrawing> variants;
    /// One for each of the entry's adjustments, in order.
    std::vector<AdjustmentDrawing> adjustments;
    std::vector<base::Diagnostic> diagnostics;

    /// Whether any variant draws.
    [[nodiscard]] bool hasDrawing() const;

    /// The drawing of the variant `placement` matches, if one matches, before
    /// any adjustment.
    [[nodiscard]] const ActorDrawing* findDrawing(const actor::PlacementValues& placement) const;

    /// The drawing for `placement`: its variant's, with the items its params
    /// pick, and every adjustment that matches it made in order. Empty when no
    /// variant matches.
    [[nodiscard]] std::optional<ActorDrawing>
    makeDrawing(const actor::PlacementValues& placement) const;
};

/// The models of every actor a scene places, in any layer, by actor ID, and the
/// objects they are drawn from.
struct SceneActors {
    std::map<s16, SceneActorModel> models;
    /// The scene's enum name, `SCENE_DEKU_TREE`, which variants may match on.
    std::optional<std::string> scene;
    std::vector<ActorObject> objects;
    /// The objects read, for reading the actors again when one changes.
    std::vector<std::filesystem::path> watched;
    std::chrono::milliseconds elapsed{};
    /// Of `elapsed`, the time spent reading objects; the rest is posing and
    /// finding symbols.
    std::chrono::milliseconds objectsElapsed{};
    /// What was wrong with the actor database the actors were read by.
    std::vector<base::Diagnostic> diagnostics;
};

/// Link's age in a scene layer: child in the first two, adult in the next two,
/// none in a cutscene layer.
[[nodiscard]] std::optional<actor::Age> findLayerAge(usize layerIndex);

/// Reads the model of every actor `layers` place, player entries and the
/// children their variants spawn included: each by its entry in `database`,
/// with the objects its entry names and every skeleton posed; an actor
/// without an entry has no drawing, and is marked. `gameplay_keep` is always read, and the scene
/// keep object, when not zero, into segment 5, as `SCENE_CMD_SPECIAL_FILES` has the game load it:
/// every object's display lists may point into either. `sceneFolder` names the scene's folder,
/// `ydan`, whose enum name the scene table gives; a variant that matches on other scenes is not
/// read.
[[nodiscard]] SceneActors readSceneActors(hackeroot::AssetImageReader& reader,
                                          std::string_view sceneFolder,
                                          const std::vector<SceneLayer>& layers,
                                          const actor::ActorDatabase& database);

/// The values a placement's variant is found by.
[[nodiscard]] actor::PlacementValues makePlacementValues(const SceneActors& actors, u16 params,
                                                         const std::array<s16, 3>& rotation,
                                                         std::optional<actor::Age> age);

} // namespace toki::scene
