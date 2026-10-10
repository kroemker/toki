#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/Pass.h"
#include "base/Diagnostic.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace toki::actor {

/// What a param's bits mean, and so how they are shown and matched.
enum class ParamKind : u8 {
    Flag,
    Number,
    Choice,
};

/// Where a param's bits are read from: the placed actor's params, or one axis
/// of its placed rotation, which some actors read more params from.
enum class ParamSource : u8 {
    Params,
    RotationX,
    RotationY,
    RotationZ,
};

/// One value a choice param may be, and what it is called.
struct ChoiceValue {
    s64 value = 0;
    std::string label;
};

/// A named range of bits of a placed actor's params or rotation, given by its
/// mask, whose bits are adjacent. A choice's values are listed in `values`, or
/// taken from the checkout's `collection` of that name, `GetItemID`; `offset`
/// is added to the param's value before it is looked up among them, as a sign
/// adds 0x300 to its message.
struct ActorParam {
    std::string name;
    ParamKind kind = ParamKind::Number;
    ParamSource source = ParamSource::Params;
    u16 mask = 0;
    bool isSigned = false;
    bool isHex = false;
    std::vector<ChoiceValue> values;
    std::optional<std::string> collection;
    s64 offset = 0;
    std::string description;
    base::SourceLocation location;
};

/// One value a param may be matched against: a flag's state or a number, or
/// for `age` and `scene`, a name.
using MatchValue = std::variant<bool, s64, std::string>;

/// What one param of a variant's `match` must be: any of `values`.
struct ParamMatch {
    std::string param;
    std::vector<MatchValue> values;
};

enum class Age : u8 {
    Child,
    Adult,
};

/// The primitive and environment colours something is drawn with.
struct DrawColors {
    std::optional<std::array<u8, 4>> primitive;
    std::optional<std::array<u8, 4>> environment;
};

/// A segment from 8 to 12 pointed at a symbol.
struct SegmentSymbol {
    u8 segment = 0;
    std::string name;
};

/// Where something is drawn relative to its actor: moved, turned in degrees
/// about Z, then Y, then X, and scaled.
struct PartPlacement {
    std::array<f32, 3> translation{};
    std::array<f32, 3> rotation{};
    std::array<f32, 3> scale{1.0F, 1.0F, 1.0F};
};

enum class PartKind : u8 {
    DisplayList,
    Skeleton,
};

/// One display list or skeleton a variant draws, with everything it is drawn
/// with: nothing carries over from one part to the next.
struct ActorPart {
    PartKind kind = PartKind::DisplayList;
    std::string symbol;
    /// Where `symbol` is, when not in the variant's object.
    std::optional<std::string> object;
    /// For a skeleton, the animation whose first frame it stands in, and the
    /// object that is in when not the part's.
    std::optional<std::string> pose;
    std::optional<std::string> poseObject;
    PartPlacement placement;
    bool isBillboard = false;
    DrawColors colors;
    std::vector<SegmentSymbol> segments;
    /// The `sSetupDL` it is drawn after, when not `SETUPDL_25`.
    std::optional<u8> setup;
    Pass pass = Pass::Opaque;
    /// Whether it is drawn with a `LookAt` from the camera toward the actor,
    /// as `func_8002EBCC` sets one for what shines by `G_TEXTURE_GEN`.
    bool isHilit = false;
    base::SourceLocation location;
};

/// An actor that another spawns, drawn by its own entry, at an offset from its
/// parent, turned with it.
struct ChildActor {
    std::string actor;
    u16 params = 0;
    std::array<f32, 3> offset{};
};

/// How an actor is drawn when it is not a model.
enum class DrawKind : u8 {
    Model,
    Marker,
    Children,
};

/// One way an actor is drawn, for the placements its `match` holds for.
/// What it leaves out, it takes from its entry.
struct ActorVariant {
    std::vector<ParamMatch> match;
    std::optional<std::string> object;
    std::optional<f32> scale;
    std::optional<f32> heightOffset;
    std::optional<DrawKind> draw;
    std::vector<ActorPart> parts;
    std::vector<ChildActor> children;
    std::optional<u16> defaultParams;
    base::SourceLocation location;
};

/// Which axes of its placed rotation an actor is drawn turned by.
struct RotationAxes {
    bool x = true;
    bool y = true;
    bool z = true;
};

/// How one actor is shown, by its enum name in the actor table: its params,
/// its variants in the order they are tried, and what is true of every
/// variant.
struct ActorEntry {
    std::string actor;
    std::string name;
    std::string notes;
    std::optional<std::string> object;
    std::optional<f32> scale;
    std::optional<f32> heightOffset;
    RotationAxes rotates;
    std::optional<DrawKind> draw;
    std::vector<ActorParam> params;
    std::vector<ActorVariant> variants;
    base::SourceLocation location;
};

/// The lowest set bit of `mask`, which a param's value is shifted down by.
[[nodiscard]] u8 findMaskShift(u16 mask);

/// How many bits `mask` spans, from its lowest set bit to its highest.
[[nodiscard]] u8 findMaskWidth(u16 mask);

/// Whether the set bits of `mask` are adjacent, as a param's must be.
[[nodiscard]] bool isMaskContiguous(u16 mask);

/// The index of the param of `params` that has `bit` of `source`, other than
/// the one at `except`, if one has.
[[nodiscard]] std::optional<usize> findBitOwner(std::span<const ActorParam> params,
                                                ParamSource source, u8 bit,
                                                std::optional<usize> except = std::nullopt);

/// Renames the param `from` of `entry` to `to`, in every match of its variants
/// too.
void renameParam(ActorEntry& entry, const std::string& from, const std::string& to);

} // namespace toki::actor
