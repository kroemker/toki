#pragma once

#include "app/actor/Types.h"
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

enum class Pass : u8 {
    Opaque,
    Translucent,
};

/// A display list `Draw` draws on its own, without a skeleton.
struct DisplayListDraw {
    ObjectSymbol displayList;
    Pass pass = Pass::Opaque;
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
    f32 scale = DefaultScale;
    std::optional<base::SourceLocation> scaleSource;
    f32 yOffset = 0.0F;
    std::vector<ModelGap> gaps;

    /// What `Actor_Init` sets before the actor's own `Init` runs.
    static constexpr f32 DefaultScale = 0.01F;

    [[nodiscard]] bool hasModel() const {
        return skeleton.has_value() || !displayLists.empty();
    }
};

[[nodiscard]] const char* getGapName(GapKind kind);

} // namespace toki::actor
