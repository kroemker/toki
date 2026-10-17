#include "ViewportView.h"

#include <string>
#include <vector>

namespace toki::viewport {

std::vector<std::string> listViewChanges(const ViewportView& view) {
    std::vector<std::string> changes;
    if (!view.overlays.isShown) {
        changes.emplace_back("Overlays Off");
    }
    return changes;
}

} // namespace toki::viewport
