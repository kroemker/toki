#include "ImageTexture.h"

#include "n64/TileDescriptor.h"
#include "z64/TextureIncludes.h"
#include "z64/TextureName.h"
#include "z64/Types.h"

#include <optional>
#include <string_view>

namespace z64 {

std::optional<ImageTexture> makeImageTexture(const std::string_view fileName,
                                             const std::string_view tag, const u32 width,
                                             const u32 height) {
    if (tag != ImageTag && tag != PaletteTag) {
        return std::nullopt;
    }
    const std::optional<TextureName> name = parseTextureName(fileName);
    if (!name) {
        return std::nullopt;
    }
    ImageTexture texture{
        .isPalette = tag == PaletteTag,
        .format = name->format,
        .size = name->size,
        .width = width,
        .height = height,
        .paletteName = name->sharedPalette.value_or(name->prefix),
        .paletteHalf = name->paletteHalf,
    };
    if (texture.isPalette) {
        texture.format = n64::TextureFormat::Rgba;
        texture.size = n64::TexelSize::Bits16;
    } else if (name->format != n64::TextureFormat::ColorIndex) {
        texture.paletteName.clear();
    }
    return texture;
}

} // namespace z64
