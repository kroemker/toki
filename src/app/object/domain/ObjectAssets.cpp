#include "ObjectAssets.h"

#include "app/object/Types.h"
#include "n64/Segment.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace toki::object {

namespace {

[[nodiscard]] bool isOneOf(const std::string_view type,
                           const std::initializer_list<std::string_view> names) {
    return std::ranges::find(names, type) != names.end();
}

[[nodiscard]] AssetKind findKind(const z64::ImageSymbol& symbol) {
    if (symbol.texture) {
        return symbol.texture->isPalette ? AssetKind::Palette : AssetKind::Texture;
    }
    const std::string_view type = symbol.type;
    if (isOneOf(type, {"SkeletonHeader", "FlexSkeletonHeader"})) {
        return AssetKind::Skeleton;
    }
    if (type == "AnimationHeader") {
        return AssetKind::Animation;
    }
    if (type == "LinkAnimationHeader") {
        return AssetKind::PlayerAnimation;
    }
    if (type == "Gfx" && symbol.isArray) {
        return AssetKind::DisplayList;
    }
    if (type == "Vtx") {
        return AssetKind::Vertices;
    }
    if (isOneOf(type, {"StandardLimb", "LodLimb", "SkinLimb"})) {
        return AssetKind::Limb;
    }
    if (type == "CollisionHeader") {
        return AssetKind::Collision;
    }
    return AssetKind::Other;
}

} // namespace

std::vector<ObjectAsset> listObjectAssets(const z64::MemoryImage& image, const u8 segment) {
    std::vector<ObjectAsset> assets;
    for (const auto& [name, symbol] : image.symbols) {
        if (symbol.segment != segment) {
            continue;
        }
        assets.push_back(ObjectAsset{
            .name = name,
            .type = symbol.type,
            .kind = findKind(symbol),
            .address = n64::pack(n64::SegmentAddress{.segment = symbol.segment,
                                                     .offset = static_cast<u32>(symbol.offset)}),
            .size = symbol.size,
        });
    }
    std::ranges::sort(assets, {}, &ObjectAsset::address);
    return assets;
}

std::string_view getAssetKindName(const AssetKind kind) {
    switch (kind) {
    case AssetKind::Skeleton:
        return "Skeletons";
    case AssetKind::Animation:
        return "Animations";
    case AssetKind::PlayerAnimation:
        return "Player animations";
    case AssetKind::DisplayList:
        return "Display lists";
    case AssetKind::Texture:
        return "Textures";
    case AssetKind::Palette:
        return "Palettes";
    case AssetKind::Vertices:
        return "Vertices";
    case AssetKind::Limb:
        return "Limbs";
    case AssetKind::Collision:
        return "Collision";
    case AssetKind::Other:
        return "Other";
    }
    return "Other";
}

} // namespace toki::object
