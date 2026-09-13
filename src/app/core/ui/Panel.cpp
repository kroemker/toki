#include "Panel.h"

#include <format>
#include <string>
#include <utility>

namespace toki::core {

Panel::Panel(std::string id, std::string title, const char* icon, DockSlot slot)
    : id(std::move(id)), menuLabel(std::format("{}  {}", icon, title)),
      windowTitle(std::format("{}  {}###{}", icon, title, this->id)), dockSlot(slot) {}

} // namespace toki::core
