#include "ObjectPanel.h"

#include "app/assets/domain/ObjectNames.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "app/object/Types.h"
#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectAssets.h"
#include "app/object/domain/ObjectView.h"
#include "app/texture/ui/TexturePanel.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "base/Diagnostic.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace toki::object {

namespace {

constexpr f32 SecondsPerFrame = 1.0F / 20.0F;
constexpr f32 SlowestSpeed = 0.05F;
constexpr f32 FastestSpeed = 4.0F;
constexpr std::array<AssetKind, 10> ListedKinds{
    AssetKind::Skeleton,  AssetKind::Animation, AssetKind::DisplayList, AssetKind::Texture,
    AssetKind::Palette,   AssetKind::Vertices,  AssetKind::Limb,        AssetKind::PlayerAnimation,
    AssetKind::Collision, AssetKind::Other};

[[nodiscard]] bool isViewable(const AssetKind kind) {
    return kind == AssetKind::Skeleton || kind == AssetKind::Animation ||
           kind == AssetKind::DisplayList || kind == AssetKind::Texture;
}

[[nodiscard]] std::optional<u32> findAddressOf(const Object& from, const Object& to,
                                               const std::optional<u32> address) {
    if (!address) {
        return std::nullopt;
    }
    const auto old = std::ranges::find(from.assets, *address, &ObjectAsset::address);
    if (old == from.assets.end()) {
        return std::nullopt;
    }
    const auto now = std::ranges::find(to.assets, old->name, &ObjectAsset::name);
    return now == to.assets.end() ? std::nullopt : std::optional(now->address);
}

} // namespace

ObjectPanel::ObjectPanel(viewport::ViewportPanel& viewportPanel,
                         texture::TexturePanel& texturePanel)
    : Panel("object", "Object", core::icons::Cube, core::DockSlot::Right),
      viewportPanel(viewportPanel), texturePanel(texturePanel) {}

void ObjectPanel::setObject(Object read) {
    const bool isNew = !object || !core::isSamePath(object->directory, read.directory);
    if (isNew) {
        object = std::move(read);
        selection = makeFirstSelection(*object);
        isPlaying = false;
        frameTime = 0.0F;
    } else {
        const ObjectSelection kept{.displayList =
                                       findAddressOf(*object, read, selection.displayList),
                                   .skeleton = findAddressOf(*object, read, selection.skeleton),
                                   .animation = findAddressOf(*object, read, selection.animation),
                                   .frame = selection.frame};
        object = std::move(read);
        selection = kept;
    }
    showSelection(isNew);
    if (isNew) {
        viewportPanel.setOpen(true);
        setOpen(true);
    }
}

void ObjectPanel::showSelection(const bool framesCamera) {
    if (!object) {
        return;
    }
    ObjectContent made = makeObjectContent(*object, selection, std::nullopt);
    if (selection.frame >= made.frameCount) {
        selection.frame = made.frameCount - 1;
        made = makeObjectContent(*object, selection, std::nullopt);
    }
    frameCount = made.frameCount;
    bounds = made.bounds;
    for (const base::Diagnostic& diagnostic : made.diagnostics) {
        core::logWarn("{}", base::describe(diagnostic));
    }
    viewportPanel.setContent(std::move(made.content));
    if (framesCamera && bounds) {
        const glm::vec3 center = (bounds->lowest + bounds->highest) / 2.0F;
        const f32 radius = glm::length(bounds->highest - bounds->lowest) / 2.0F;
        viewportPanel.frameView(center, radius);
    }
}

void ObjectPanel::showFrame() {
    if (!object) {
        return;
    }
    viewportPanel.setNextFrame(makeObjectContent(*object, selection, bounds).content);
}

void ObjectPanel::selectAsset(const ObjectAsset& asset) {
    if (asset.kind == AssetKind::DisplayList) {
        selection = ObjectSelection{.displayList = asset.address};
    } else if (asset.kind == AssetKind::Skeleton) {
        selection = ObjectSelection{.skeleton = asset.address};
    } else if (asset.kind == AssetKind::Texture) {
        texturePanel.selectTexture(asset.name);
        return;
    } else if (asset.kind == AssetKind::Animation) {
        std::optional<u32> skeleton = selection.skeleton;
        const std::optional<u32> fitting = findFittingSkeleton(*object, asset.address);
        if (fitting) {
            skeleton = fitting;
        }
        if (!skeleton) {
            core::logWarn("no skeleton fits {}", asset.name);
            return;
        }
        const bool keepsSkeleton = skeleton == selection.skeleton;
        selection = ObjectSelection{.skeleton = skeleton, .animation = asset.address};
        isPlaying = false;
        frameTime = 0.0F;
        showSelection(!keepsSkeleton);
        return;
    } else {
        return;
    }
    showSelection(true);
}

bool ObjectPanel::isSelected(const ObjectAsset& asset) const {
    return asset.address == selection.displayList || asset.address == selection.skeleton ||
           asset.address == selection.animation;
}

void ObjectPanel::advancePlayback() {
    if (!selection.animation || !isPlaying || frameCount <= 1) {
        return;
    }
    frameTime += ImGui::GetIO().DeltaTime * playbackSpeed;
    if (frameTime < SecondsPerFrame) {
        return;
    }
    while (frameTime >= SecondsPerFrame) {
        frameTime -= SecondsPerFrame;
        selection.frame = (selection.frame + 1) % frameCount;
    }
    showFrame();
}

void ObjectPanel::draw() {
    advancePlayback();
    if (!object) {
        ImGui::TextColored(core::palette::TextMuted, "Open an object in the Assets panel.");
        return;
    }
    const std::optional<std::string_view> readable = assets::findObjectName(object->name);
    const std::string title(readable.value_or(object->name));
    const std::string details =
        readable ? std::format("{} · {} assets", object->name, object->assets.size())
                 : std::format("{} assets", object->assets.size());
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(core::palette::TextMuted, "%s", details.c_str());
    drawAnimationControls();
    ImGui::Separator();
    if (ImGui::BeginChild("##assets")) {
        for (const AssetKind kind : ListedKinds) {
            drawKind(*object, kind, isViewable(kind) && kind != AssetKind::Texture);
        }
    }
    ImGui::EndChild();
}

void ObjectPanel::drawAnimationControls() {
    if (!selection.animation) {
        return;
    }
    if (ImGui::Button(isPlaying ? core::icons::Pause : core::icons::Play)) {
        isPlaying = !isPlaying;
    }
    ImGui::SameLine();
    int frame = static_cast<int>(selection.frame);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##frame", &frame, 0, static_cast<int>(frameCount) - 1,
                         std::format("frame %d of {}", frameCount).c_str())) {
        selection.frame = static_cast<usize>(frame);
        isPlaying = false;
        showFrame();
    }
    if (ImGui::Button("1x")) {
        playbackSpeed = 1.0F;
    }
    ImGui::SetItemTooltip("Play at the game's speed, 20 frames a second");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##speed", &playbackSpeed, SlowestSpeed, FastestSpeed, "%.2fx speed",
                       ImGuiSliderFlags_Logarithmic);
}

void ObjectPanel::clearObject() {
    object.reset();
    selection = {};
    bounds.reset();
    frameCount = 1;
    isPlaying = false;
    frameTime = 0.0F;
}

void ObjectPanel::drawKind(const Object& shown, const AssetKind kind, const bool isOpen) {
    const auto count =
        static_cast<usize>(std::ranges::count(shown.assets, kind, &ObjectAsset::kind));
    if (count == 0) {
        return;
    }
    const std::string header =
        std::format("{} ({})###{}", getAssetKindName(kind), count, static_cast<int>(kind));
    if (!ImGui::CollapsingHeader(header.c_str(), isOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }
    const bool viewable = isViewable(kind);
    for (const ObjectAsset& asset : shown.assets) {
        if (asset.kind != kind) {
            continue;
        }
        ImGui::PushID(asset.name.c_str());
        if (viewable) {
            if (ImGui::Selectable(asset.name.c_str(), isSelected(asset))) {
                selectAsset(asset);
            }
            if (kind == AssetKind::DisplayList && inspectDisplayList != nullptr &&
                ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Inspect in Display List")) {
                    selectAsset(asset);
                    inspectDisplayList(asset.name);
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextUnformatted(asset.name.c_str());
        }
        ImGui::SetItemTooltip("%s at 0x%08X, %zu bytes", asset.type.c_str(), asset.address,
                              asset.size);
        ImGui::PopID();
    }
}

} // namespace toki::object
