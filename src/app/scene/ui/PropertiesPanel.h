#pragma once

#include "app/core/ui/Panel.h"

namespace toki::scene {

class ScenePanel;

/// The properties of what is selected in the Scene panel, beside it.
class PropertiesPanel : public core::Panel {
public:
    explicit PropertiesPanel(ScenePanel& scenePanel);

    void draw() override;

private:
    ScenePanel& scenePanel;
};

} // namespace toki::scene
