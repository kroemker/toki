#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorModel.h"
#include "base/Diagnostic.h"

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace toki::actor {

/// What a field's bits mean, and so how they are shown and matched.
enum class FieldKind : u8 {
    Flag,
    Enum,
    Integer,
};

/// Where a field's bits are read from: the placed actor's params, or one axis
/// of its placed rotation, which some actors read extra parameters from.
enum class FieldSource : u8 {
    Params,
    RotationX,
    RotationY,
    RotationZ,
};

/// What an integer field holds, for later panels to link it to.
enum class FieldRole : u8 {
    None,
    SwitchFlag,
    ChestFlag,
    CollectibleFlag,
    ClearFlag,
    Path,
    TextId,
    Item,
    Actor,
};

/// One named value of an enum field, as a constant is.
struct EnumValue {
    std::string name;
    s64 value = 0;
};

/// A named range of bits of a placed actor's params or rotation.
struct ParamField {
    std::string name;
    FieldKind kind = FieldKind::Integer;
    FieldSource source = FieldSource::Params;
    u8 shift = 0;
    u8 width = 1;
    bool isSigned = false;
    bool isHex = false;
    std::vector<EnumValue> values;
    FieldRole role = FieldRole::None;
    std::string meaning;
    base::SourceLocation location;
};

/// What one field of a variant's `match` must be: a flag's state, an enum's
/// value by name or number, or a number.
struct FieldMatch {
    std::string field;
    std::variant<bool, s64, std::string> value;
};

enum class Age : u8 {
    Child,
    Adult,
};

enum class TimeOfDay : u8 {
    Day,
    Night,
};

/// What an actor needs to exist.
struct SpawnCondition {
    std::optional<Age> age;
    std::optional<TimeOfDay> time;
    /// A flag field that must be set, or unset.
    std::optional<std::string> flagField;
    bool isFlagSet = true;
};

/// The primitive and environment colours something is drawn with.
struct DrawColors {
    std::optional<std::array<u8, 4>> primitive;
    std::optional<std::array<u8, 4>> environment;
};

/// A segment from 8 to 12 pointed at a symbol of the variant's object.
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

/// One display list a variant draws, and how. Its object is the variant's
/// unless it names one of its own.
struct DisplayListPart {
    std::string name;
    std::optional<std::string> object;
    Pass pass = Pass::Opaque;
    PartPlacement placement;
    DrawColors colors;
    std::optional<bool> isBillboard;
    std::vector<SegmentSymbol> segments;
};

/// An actor that another spawns, drawn by its own entry, at an offset from its
/// parent, turned with it.
struct ChildActor {
    std::string actor;
    u16 params = 0;
    std::array<f32, 3> offset{};
};

/// How a variant is drawn when it is not a model.
enum class DrawKind : u8 {
    Model,
    Marker,
    Children,
};

enum class ShadowKind : u8 {
    None,
    Circle,
    WhiteCircle,
    Horse,
    Feet,
};

struct Shadow {
    ShadowKind kind = ShadowKind::None;
    f32 size = 0.0F;
};

struct PointLight {
    std::array<u8, 3> color{};
    f32 radius = 0.0F;
    std::array<f32, 3> offset{};
};

/// One way an actor is drawn, for the placements its `match` holds for.
/// What it leaves out, it takes from its entry.
struct ActorVariant {
    std::vector<FieldMatch> match;
    std::optional<std::string> object;
    std::optional<f32> scale;
    std::optional<f32> heightOffset;
    std::optional<DrawKind> draw;
    std::optional<std::string> skeleton;
    std::optional<std::string> animation;
    /// The object the animation is in, when it is not the variant's, as
    /// actors that share animations take them from another object.
    std::optional<std::string> animationObject;
    std::optional<u32> frame;
    std::vector<DisplayListPart> displayLists;
    std::vector<SegmentSymbol> segments;
    DrawColors colors;
    std::optional<bool> isBillboard;
    std::vector<ChildActor> children;
    std::optional<SpawnCondition> spawn;
    std::optional<u16> defaultParams;
    base::SourceLocation location;
};

enum class EntryStatus : u8 {
    Guessed,
    Checked,
};

/// Where a guessed entry was read from, and when that file last changed then.
struct GuessSource {
    std::string file;
    std::string modified;
};

/// Which axes of its placed rotation an actor is drawn turned by.
struct RotationAxes {
    bool x = true;
    bool y = true;
    bool z = true;
};

/// How one actor is drawn and what it is, by its enum name in the actor
/// table: its fields, its variants in the order they are tried, and what is
/// true of every variant.
struct ActorEntry {
    std::string actor;
    EntryStatus status = EntryStatus::Checked;
    std::optional<GuessSource> guessedFrom;
    bool isUncertain = false;
    std::string name;
    std::string description;
    std::string notes;
    std::optional<std::string> object;
    std::optional<f32> scale;
    std::optional<f32> heightOffset;
    RotationAxes rotates;
    std::optional<DrawKind> draw;
    std::vector<ParamField> fields;
    std::vector<ActorVariant> variants;
    std::optional<Shadow> shadow;
    std::optional<SpawnCondition> spawn;
    std::optional<std::string> collision;
    std::vector<PointLight> lights;
    std::vector<u16> textIds;
    base::SourceLocation location;
};

} // namespace toki::actor
