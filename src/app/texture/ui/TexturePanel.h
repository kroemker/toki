#pragma once

#include "app/core/ui/Panel.h"
#include "app/texture/Types.h"
#include "app/texture/domain/TextureCatalog.h"
#include "gfx/Device.h"
#include "gfx/ImageTexture.h"
#include "z64/MemoryImage.h"

#include <imgui.h>

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace toki::texture {

/// One image whose textures are listed, under a label such as a room's name.
struct TextureGroup {
    std::string label;
    /// Shared with the viewport content the image was read for, so the
    /// textures stay listed whichever of the two lets go of it first.
    std::shared_ptr<const z64::MemoryImage> image;
};

/// The textures of what is open, as a grid of thumbnails over the one picked,
/// shown large with its format, size and palette.
class TexturePanel : public core::Panel {
public:
    explicit TexturePanel(const gfx::Device& device);

    /// Lists every texture of `groups` under `title`, keeping the one picked
    /// when it is still there by name.
    void setTextures(std::string title, std::vector<TextureGroup> groups);

    void clearTextures();

    /// Picks the texture named `name`, and brings the panel forward.
    void selectTexture(const std::string& name);

    void draw() override;

private:
    struct Item {
        usize group = 0;
        TextureEntry entry;
        std::optional<std::expected<DecodedTexture, std::string>> decoded;
        std::optional<gfx::ImageTexture> thumbnail;
        bool hasFailedUpload = false;
    };

    void drawGallery();
    void drawThumbnail(usize index);
    void drawDetail();
    void drawDetailImage(const DecodedTexture& decoded);
    [[nodiscard]] const std::expected<DecodedTexture, std::string>& findDecoded(Item& item);
    [[nodiscard]] bool matchesFilter(const Item& item) const;

    const gfx::Device& device;
    std::string title;
    std::vector<TextureGroup> groups;
    std::vector<Item> items;
    std::optional<usize> selected;
    std::optional<gfx::ImageTexture> detail;
    std::optional<usize> detailItem;
    u32 detailScale = 0;
    int zoom = 0;
    bool showsGrid = false;
    std::array<char, 128> filter{};
};

} // namespace toki::texture
