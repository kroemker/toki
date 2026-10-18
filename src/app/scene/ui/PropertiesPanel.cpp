#include "PropertiesPanel.h"

#include "app/core/ui/Icons.h"
#include "app/scene/ui/ScenePanel.h"

namespace toki::scene {

PropertiesPanel::PropertiesPanel(ScenePanel& scenePanel)
    : Panel("properties", "Properties", core::icons::SlidersHorizontal,
            core::DockSlot::RightBottom),
      scenePanel(scenePanel) {}

void PropertiesPanel::draw() {
    scenePanel.drawProperties();
}

} // namespace toki::scene
