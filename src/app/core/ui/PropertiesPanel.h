#pragma once

#include "app/core/Types.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/PropertySource.h"

#include <optional>
#include <vector>

namespace toki::core {

/// The properties of what was selected last, in whichever panel it was
/// selected: a source whose selection changes takes the panel over.
class PropertiesPanel : public Panel {
public:
    PropertiesPanel();

    /// Adds a source, which must outlive the panel. The first is shown until
    /// another selects something.
    void addSource(PropertySource& source);

    void draw() override;

private:
    struct Watched {
        PropertySource* source = nullptr;
        std::optional<u64> lastKey;
    };

    void update();

    std::vector<Watched> sources;
    usize current = 0;
};

} // namespace toki::core
