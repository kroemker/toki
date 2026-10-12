#pragma once

#include "app/core/ui/Panel.h"
#include "app/object/Types.h"
#include "app/object/domain/Animation.h"
#include "app/object/domain/Object.h"
#include "app/object/domain/ObjectAssets.h"
#include "app/object/domain/ObjectView.h"
#include "app/texture/ui/TexturePanel.h"
#include "app/viewport/ui/ViewportPanel.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace toki::object {

/// The open object: everything it holds, by kind, and which of it the
/// viewport shows. A skeleton shows at rest or animated, a display list on
/// its own.
class ObjectPanel : public core::Panel {
public:
    ObjectPanel(viewport::ViewportPanel& viewportPanel, texture::TexturePanel& texturePanel);

    /// Shows `object` in the viewport. An object read again keeps what was
    /// shown of it and where the camera is; another one starts afresh, with
    /// its first skeleton at the first frame of its first animation that fits.
    void setObject(Object read);

    /// Forgets the open object, when something else takes the viewport.
    void clearObject();

    /// What "Inspect in Display List" opens a display list with, by its name.
    void setDisplayListInspector(std::function<void(const std::string&)> inspector) {
        inspectDisplayList = std::move(inspector);
    }

    void draw() override;

private:
    void showSelection(bool framesCamera);
    void showFrame();
    void advancePlayback();
    void drawAnimationControls();
    void drawKind(const Object& shown, AssetKind kind, bool isOpen);
    void selectAsset(const ObjectAsset& asset);
    [[nodiscard]] bool isSelected(const ObjectAsset& asset) const;

    viewport::ViewportPanel& viewportPanel;
    texture::TexturePanel& texturePanel;
    std::optional<Object> object;
    ObjectSelection selection;
    std::optional<Bounds> bounds;
    usize frameCount = 1;
    bool isPlaying = false;
    f32 frameTime = 0.0F;
    f32 playbackSpeed = 1.0F;
    std::function<void(const std::string&)> inspectDisplayList;
};

} // namespace toki::object
