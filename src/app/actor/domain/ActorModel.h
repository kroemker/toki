#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/Pass.h"
#include "base/Diagnostic.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// A symbol an overlay names, the object whose header declares it, and where
/// the overlay names it.
struct ObjectSymbol {
    std::string name;
    std::string object;
    std::string type;
    base::SourceLocation location;
};

/// A skeleton set up in `Init`, and the animation it is posed with, if one is
/// named.
struct SkeletonModel {
    ObjectSymbol skeleton;
    bool isFlexible = false;
    std::optional<ObjectSymbol> animation;
};

/// A segment `Draw` points at a symbol of an object for one pass, as at an
/// eye texture: the game's opaque and translucent display lists each have
/// their own.
struct SegmentSetting {
    u8 segment = 0;
    Pass pass = Pass::Opaque;
    ObjectSymbol symbol;
};

/// A display list `Draw` draws on its own, without a skeleton, and the
/// segments its pass has pointed by then.
struct DisplayListDraw {
    ObjectSymbol displayList;
    Pass pass = Pass::Opaque;
    std::vector<SegmentSetting> segments;
};

/// Why part of how an actor is drawn could not be read from its C.
enum class GapKind : u8 {
    NoOverlay,
    NoProfile,
    NoDraw,
    SkeletonNotLiteral,
    SkinSkeleton,
    AnimationNotLiteral,
    DisplayListNotLiteral,
    SymbolNotInObject,
    OverlayData,
    ScaleNotLiteral,
    SegmentNotLiteral,
    NothingDrawn,
};

struct ModelGap {
    GapKind kind = GapKind::NothingDrawn;
    base::Diagnostic diagnostic;
};

/// How an actor is drawn, as far as its overlay's C says it in literals: the
/// object its profile names, a skeleton or else display lists, and the scale
/// and height offset its `Init` sets. Everything left unread is a gap, with
/// the reason and where.
struct ActorModel {
    std::string actorName;
    std::string profileObject;
    std::optional<SkeletonModel> skeleton;
    std::vector<DisplayListDraw> displayLists;
    /// Segments 8 to 12 as `Draw` points them for a skeleton, the first
    /// setting of each a pass.
    std::vector<SegmentSetting> segments;
    f32 scale = DefaultScale;
    std::optional<base::SourceLocation> scaleSource;
    f32 yOffset = 0.0F;
    std::vector<ModelGap> gaps;
    /// The params its C reads, as `findActorParams` finds them.
    std::vector<ActorParam> params;

    /// What `Actor_Init` sets before the actor's own `Init` runs.
    static constexpr f32 DefaultScale = 0.01F;

    [[nodiscard]] bool hasModel() const {
        return skeleton.has_value() || !displayLists.empty();
    }
};

[[nodiscard]] const char* getGapName(GapKind kind);

} // namespace toki::actor
