#include "TexturePanel.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/TextSearch.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/texture/Types.h"
#include "app/texture/domain/TextureCatalog.h"
#include "base/Diagnostic.h"
#include "gfx/Device.h"
#include "gfx/ImageTexture.h"
#include "n64/Color.h"
#include "z64/MemoryImage.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::texture {

namespace {

constexpr f32 ThumbnailSize = 64.0F;
constexpr f32 CellPadding = 6.0F;
constexpr f32 CheckerSize = 8.0F;
constexpr f32 GalleryShare = 0.45F;
constexpr int MaximumZoom = 16;
constexpr u32 MaximumScaledSide = 2048;
constexpr int GridZoom = 4;

[[nodiscard]] ImTextureID makeTextureId(const gfx::ImageTexture& texture) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.getTexture()));
}

[[nodiscard]] std::optional<gfx::ImageTexture>
makeUpload(const gfx::Device& device, const DecodedTexture& decoded, const u32 scale) {
    const std::vector<u8> pixels = makeScaledPixels(decoded, scale);
    std::expected<gfx::ImageTexture, base::Diagnostic> uploaded =
        gfx::ImageTexture::create(device, decoded.width * scale, decoded.height * scale, pixels);
    if (!uploaded) {
        core::logError("{}", base::describe(uploaded.error()));
        return std::nullopt;
    }
    return std::move(*uploaded);
}

void drawCheckerboard(ImDrawList& drawList, const ImVec2 low, const ImVec2 high) {
    const ImVec2 clipLow = drawList.GetClipRectMin();
    const ImVec2 clipHigh = drawList.GetClipRectMax();
    const ImU32 light = IM_COL32(0x60, 0x60, 0x66, 0xFF);
    const ImU32 dark = IM_COL32(0x40, 0x40, 0x46, 0xFF);
    drawList.AddRectFilled(low, high, dark);
    const f32 startX = std::max(low.x, clipLow.x - CheckerSize);
    const f32 startY = std::max(low.y, clipLow.y - CheckerSize);
    const auto firstColumn = static_cast<int>(std::floor((startX - low.x) / CheckerSize));
    const auto firstRow = static_cast<int>(std::floor((startY - low.y) / CheckerSize));
    for (int row = firstRow;; ++row) {
        const f32 y = low.y + (static_cast<f32>(row) * CheckerSize);
        if (y >= high.y || y > clipHigh.y) {
            break;
        }
        for (int column = firstColumn + ((firstColumn + row) % 2 == 0 ? 0 : 1);; column += 2) {
            const f32 x = low.x + (static_cast<f32>(column) * CheckerSize);
            if (x >= high.x || x > clipHigh.x) {
                break;
            }
            drawList.AddRectFilled(
                ImVec2(x, y),
                ImVec2(std::min(x + CheckerSize, high.x), std::min(y + CheckerSize, high.y)),
                light);
        }
    }
}

void drawPixelGrid(ImDrawList& drawList, const ImVec2 low, const DecodedTexture& texture,
                   const f32 step) {
    const ImVec2 clipLow = drawList.GetClipRectMin();
    const ImVec2 clipHigh = drawList.GetClipRectMax();
    const ImVec2 high(low.x + (static_cast<f32>(texture.width) * step),
                      low.y + (static_cast<f32>(texture.height) * step));
    const ImU32 color = IM_COL32(0, 0, 0, 0x50);
    for (u32 column = 0; column <= texture.width; ++column) {
        const f32 x = low.x + (static_cast<f32>(column) * step);
        if (x >= clipLow.x && x <= clipHigh.x) {
            drawList.AddLine(ImVec2(x, std::max(low.y, clipLow.y)),
                             ImVec2(x, std::min(high.y, clipHigh.y)), color);
        }
    }
    for (u32 row = 0; row <= texture.height; ++row) {
        const f32 y = low.y + (static_cast<f32>(row) * step);
        if (y >= clipLow.y && y <= clipHigh.y) {
            drawList.AddLine(ImVec2(std::max(low.x, clipLow.x), y),
                             ImVec2(std::min(high.x, clipHigh.x), y), color);
        }
    }
}

[[nodiscard]] std::string describeTexture(const TextureEntry& entry) {
    std::string described =
        std::format("{} · {}x{} · {} bytes · 0x{:08X}", getFormatName(entry.texture),
                    entry.texture.width, entry.texture.height, entry.size, entry.address);
    if (entry.palette) {
        described += std::format(" · palette {}", entry.palette->name);
    }
    return described;
}

} // namespace

TexturePanel::TexturePanel(const gfx::Device& device)
    : Panel("textures", "Textures", core::icons::Images, core::DockSlot::Centre), device(device) {}

void TexturePanel::setTextures(std::string shownTitle, std::vector<TextureGroup> shownGroups) {
    const std::optional<std::string> kept =
        selected ? std::optional(items[*selected].entry.name) : std::nullopt;
    clearTextures();
    title = std::move(shownTitle);
    groups = std::move(shownGroups);
    std::vector<const z64::MemoryImage*> paletteImages;
    paletteImages.reserve(groups.size());
    for (const TextureGroup& group : groups) {
        paletteImages.push_back(group.image.get());
    }
    for (usize group = 0; group < groups.size(); ++group) {
        for (TextureEntry& entry : listTextures(*groups[group].image, paletteImages)) {
            items.push_back(Item{.group = group, .entry = std::move(entry)});
        }
    }
    if (kept) {
        const auto found =
            std::ranges::find(items, *kept, [](const Item& item) { return item.entry.name; });
        if (found != items.end()) {
            selected = static_cast<usize>(found - items.begin());
        }
    }
    if (!selected && !items.empty()) {
        selected = 0;
    }
}

void TexturePanel::clearTextures() {
    title.clear();
    groups.clear();
    items.clear();
    selected.reset();
    detail.reset();
    detailItem.reset();
    detailScale = 0;
}

void TexturePanel::selectTexture(const std::string& name) {
    const auto found =
        std::ranges::find(items, name, [](const Item& item) { return item.entry.name; });
    if (found == items.end()) {
        return;
    }
    selected = static_cast<usize>(found - items.begin());
    setOpen(true);
    ImGui::SetWindowFocus(getWindowTitle().c_str());
}

const std::expected<DecodedTexture, std::string>& TexturePanel::findDecoded(Item& item) {
    if (!item.decoded) {
        item.decoded = decodeTexture(*groups[item.group].image, item.entry);
    }
    return *item.decoded;
}

bool TexturePanel::matchesFilter(const Item& item) const {
    return core::containsIgnoringCase(item.entry.name, std::string_view(filter.data()));
}

void TexturePanel::draw() {
    if (items.empty()) {
        ImGui::TextColored(core::palette::TextMuted,
                           title.empty() ? "Open an object or a scene to see its textures."
                                         : "It holds no textures.");
        return;
    }
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%zu textures", items.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name", filter.data(), filter.size());

    const f32 height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("##gallery", ImVec2(0.0F, std::floor(height * GalleryShare)),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
        drawGallery();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##detail", ImVec2(0.0F, 0.0F))) {
        drawDetail();
    }
    ImGui::EndChild();
}

void TexturePanel::drawGallery() {
    const f32 cell = ThumbnailSize + (CellPadding * 2.0F);
    const f32 right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (usize group = 0; group < groups.size(); ++group) {
        if (groups.size() > 1) {
            const auto count = std::ranges::count_if(items, [&](const Item& item) {
                return item.group == group && matchesFilter(item);
            });
            if (count == 0) {
                continue;
            }
            const std::string header =
                std::format("{} ({})###group{}", groups[group].label, count, group);
            if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }
        }
        bool isFirst = true;
        for (usize index = 0; index < items.size(); ++index) {
            if (items[index].group != group || !matchesFilter(items[index])) {
                continue;
            }
            if (!isFirst &&
                ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + cell <= right) {
                ImGui::SameLine();
            }
            isFirst = false;
            drawThumbnail(index);
        }
    }
}

void TexturePanel::drawThumbnail(const usize index) {
    Item& item = items[index];
    const f32 cell = ThumbnailSize + (CellPadding * 2.0F);
    const f32 labelHeight = ImGui::GetTextLineHeight();
    ImGui::PushID(static_cast<int>(index));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cell, cell + labelHeight));
    const bool isVisible = ImGui::IsItemVisible();
    if (clicked) {
        selected = index;
    }
    ImGui::SetItemTooltip("%s\n%s", item.entry.name.c_str(), describeTexture(item.entry).c_str());
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const bool isSelected = selected == index;
    if (isSelected || ImGui::IsItemHovered()) {
        drawList.AddRectFilled(origin, ImVec2(origin.x + cell, origin.y + cell + labelHeight),
                               ImGui::GetColorU32(isSelected ? core::palette::AccentMuted
                                                             : core::palette::SurfaceHovered),
                               4.0F);
    }
    if (isVisible) {
        if (const std::expected<DecodedTexture, std::string>& decoded = findDecoded(item)) {
            const f32 longest = static_cast<f32>(std::max(decoded->width, decoded->height));
            const auto scale =
                static_cast<u32>(std::max(1.0F, std::floor(ThumbnailSize / longest)));
            if (!item.thumbnail && !item.hasFailedUpload) {
                item.thumbnail = makeUpload(device, *decoded, scale);
                item.hasFailedUpload = !item.thumbnail;
            }
            const f32 fit = std::min(1.0F, ThumbnailSize / (longest * static_cast<f32>(scale)));
            const ImVec2 size(static_cast<f32>(decoded->width * scale) * fit,
                              static_cast<f32>(decoded->height * scale) * fit);
            const ImVec2 low(origin.x + CellPadding + ((ThumbnailSize - size.x) / 2.0F),
                             origin.y + CellPadding + ((ThumbnailSize - size.y) / 2.0F));
            const ImVec2 high(low.x + size.x, low.y + size.y);
            drawCheckerboard(drawList, low, high);
            if (item.thumbnail) {
                drawList.AddImage(makeTextureId(*item.thumbnail), low, high);
            }
        } else {
            drawList.AddText(ImVec2(origin.x + CellPadding, origin.y + CellPadding),
                             ImGui::GetColorU32(core::palette::Danger), "?");
        }
        const ImVec2 labelLow(origin.x + 2.0F, origin.y + cell);
        const ImVec4 clip(origin.x, origin.y, origin.x + cell - 2.0F,
                          origin.y + cell + labelHeight);
        drawList.AddText(nullptr, 0.0F, labelLow, ImGui::GetColorU32(core::palette::TextMuted),
                         item.entry.name.c_str(), nullptr, 0.0F, &clip);
    }
    ImGui::PopID();
}

void TexturePanel::drawDetail() {
    if (!selected || *selected >= items.size()) {
        ImGui::TextColored(core::palette::TextMuted, "Pick a texture above.");
        return;
    }
    Item& item = items[*selected];
    ImGui::TextUnformatted(item.entry.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%s", describeTexture(item.entry).c_str());
    const std::expected<DecodedTexture, std::string>& decoded = findDecoded(item);
    if (!decoded) {
        ImGui::TextColored(core::palette::Danger, "%s", decoded.error().c_str());
        return;
    }
    if (decoded->note) {
        ImGui::TextColored(core::palette::Warning, "%s", decoded->note->c_str());
    }
    ImGui::SetNextItemWidth(160.0F);
    ImGui::SliderInt("Zoom", &zoom, 0, MaximumZoom, zoom == 0 ? "fit" : "%dx",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::Checkbox("Pixel grid", &showsGrid);
    drawDetailImage(*decoded);
}

void TexturePanel::drawDetailImage(const DecodedTexture& decoded) {
    if (!ImGui::BeginChild("##image", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const u32 longest = std::max(decoded.width, decoded.height);
    u32 scale = 1;
    if (zoom == 0) {
        const f32 fit = std::min(available.x / static_cast<f32>(decoded.width),
                                 available.y / static_cast<f32>(decoded.height));
        scale = static_cast<u32>(std::clamp(std::floor(fit), 1.0F, static_cast<f32>(MaximumZoom)));
    } else {
        scale = static_cast<u32>(zoom);
    }
    scale = std::max(1U, std::min(scale, MaximumScaledSide / longest));
    const usize index = selected.value_or(0);
    if (detailItem != index || detailScale != scale) {
        detail = makeUpload(device, decoded, scale);
        detailItem = index;
        detailScale = scale;
    }
    const ImVec2 size(static_cast<f32>(decoded.width * scale),
                      static_cast<f32>(decoded.height * scale));
    const ImVec2 low = ImGui::GetCursorScreenPos();
    const ImVec2 high(low.x + size.x, low.y + size.y);
    ImGui::Dummy(size);
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawCheckerboard(drawList, low, high);
    if (detail) {
        drawList.AddImage(makeTextureId(*detail), low, high);
    }
    if (showsGrid && scale >= GridZoom) {
        drawPixelGrid(drawList, low, decoded, static_cast<f32>(scale));
    }
    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const auto x = static_cast<u32>((mouse.x - low.x) / static_cast<f32>(scale));
        const auto y = static_cast<u32>((mouse.y - low.y) / static_cast<f32>(scale));
        if (x < decoded.width && y < decoded.height) {
            const n64::Color& color = decoded.pixels[(usize{y} * decoded.width) + x];
            ImGui::SetTooltip("%u, %u\nR %u G %u B %u A %u", x, y, color[0], color[1], color[2],
                              color[3]);
        }
    }
    ImGui::EndChild();
}

} // namespace toki::texture
