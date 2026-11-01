#include "PropertiesPanel.h"

#include "app/core/ui/Icons.h"

#include <optional>

namespace toki::core {

PropertiesPanel::PropertiesPanel()
    : Panel("properties", "Properties", icons::SlidersHorizontal, DockSlot::RightBottom) {}

void PropertiesPanel::addSource(PropertySource& source) {
    sources.push_back(Watched{.source = &source, .lastKey = source.findSelectionKey()});
}

void PropertiesPanel::update() {
    for (usize index = 0; index < sources.size(); ++index) {
        Watched& watched = sources[index];
        const std::optional<u64> key = watched.source->findSelectionKey();
        if (key && key != watched.lastKey) {
            current = index;
        }
        watched.lastKey = key;
    }
}

void PropertiesPanel::draw() {
    if (sources.empty()) {
        return;
    }
    update();
    sources[current].source->drawProperties();
}

} // namespace toki::core
