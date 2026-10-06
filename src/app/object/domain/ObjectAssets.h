#pragma once

#include "app/object/Types.h"
#include "z64/MemoryImage.h"

#include <string>
#include <string_view>
#include <vector>

namespace toki::object {

/// What an object holds, by what the viewer can do with it.
enum class AssetKind : u8 {
    Skeleton,
    Animation,
    PlayerAnimation,
    DisplayList,
    Texture,
    Palette,
    Vertices,
    Limb,
    Collision,
    Other,
};

/// One symbol an object defines, and the kind its declared type makes it.
struct ObjectAsset {
    std::string name;
    std::string type;
    AssetKind kind = AssetKind::Other;
    u32 address = 0;
    usize size = 0;
};

/// Every symbol `image` lays out in `segment`, in the order it lies in the
/// segment, with its kind read from the type it was declared with. Integer
/// arrays are textures or palettes by the names the extraction and fast64
/// give them (`...Tex`, `...TLUT`); a skeleton is flexible or not by its
/// header type.
[[nodiscard]] std::vector<ObjectAsset> listObjectAssets(const z64::MemoryImage& image, u8 segment);

/// What a kind is called, in the plural, for listing.
[[nodiscard]] std::string_view getAssetKindName(AssetKind kind);

} // namespace toki::object
