#include "ActorModel.h"

namespace toki::actor {

const char* getGapName(const GapKind kind) {
    switch (kind) {
    case GapKind::NoOverlay:
        return "no overlay";
    case GapKind::NoProfile:
        return "no profile";
    case GapKind::NoDraw:
        return "no Draw";
    case GapKind::SkeletonNotLiteral:
        return "skeleton not a symbol";
    case GapKind::SkinSkeleton:
        return "skin skeleton";
    case GapKind::AnimationNotLiteral:
        return "animation not a symbol";
    case GapKind::DisplayListNotLiteral:
        return "display list not a symbol";
    case GapKind::SymbolNotInObject:
        return "symbol in no object";
    case GapKind::OverlayData:
        return "overlay's own data";
    case GapKind::ScaleNotLiteral:
        return "scale not a constant";
    case GapKind::SegmentNotLiteral:
        return "segment not a symbol";
    case GapKind::NothingDrawn:
        return "nothing drawn";
    }
    return "unknown";
}

} // namespace toki::actor
