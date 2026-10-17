#include "ViewportView.h"

#include "app/viewport/Types.h"
#include "app/viewport/domain/RenderMode.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr auto ShadingLabels = std::to_array<std::string_view>(
    {"N64", "Shade Only", "Texture Only", "Normals", "Depth", "Wireframe"});

static_assert(ShadingLabels.size() == ShadingCount,
              "a shading was added to the enum without a label");

} // namespace

std::string_view getShadingLabel(const Shading shading) {
    return ShadingLabels[static_cast<usize>(shading)];
}

std::vector<std::string> listViewChanges(const ViewportView& view) {
    const RenderMode& mode = view.renderMode;
    std::vector<std::string> changes;
    if (mode.shading != Shading::N64) {
        changes.emplace_back(getShadingLabel(mode.shading));
    }
    const std::array<std::pair<bool, std::string_view>, 5> switches{{
        {mode.lighting, "No Lighting"},
        {mode.fog, "No Fog"},
        {mode.culling, "No Culling"},
        {mode.alphaCutout, "No Alpha Cutout"},
        {mode.translucency, "No Translucency"},
    }};
    for (const auto& [isOn, change] : switches) {
        if (!isOn) {
            changes.emplace_back(change);
        }
    }
    if (mode.textureFilter == TextureFilter::Point) {
        changes.emplace_back("Point Filter");
    }
    if (!view.overlays.isShown) {
        changes.emplace_back("Overlays Off");
    }
    return changes;
}

} // namespace toki::viewport
