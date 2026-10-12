#pragma once

#include "app/tools/configure/Types.h"
#include "app/tools/configure/domain/ConfigSettings.h"
#include "app/tools/configure/domain/MemoryLayout.h"
#include "hackeroot/BuildMap.h"

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace toki::configure {

/// The Memory tab of Configure: RDRAM as a strip, laid out by the settings as
/// they are edited and by the build's map, a line on what is hovered, and the
/// settings as rows in address order among the regions they size. A region no
/// setting sizes has a row too, unless only the settings are shown.
class MemoryMap {
public:
    using DrawValue = std::function<void(const ConfigSetting&)>;

    /// Draws `group`, `config_memory.h`, with `values` as edited so far.
    /// `buildMap` is null while it is read; `drawValue` draws the control a
    /// setting is edited with.
    void draw(const ConfigGroup& group,
              const std::map<std::string, std::string, std::less<>>& values,
              const std::expected<hackeroot::BuildMap, std::string>* buildMap,
              const DrawValue& drawValue);

private:
    void drawFilter();
    void drawTable(const ConfigGroup& group, const MemoryLayout* layout,
                   const std::map<std::string, std::string, std::less<>>& values,
                   const DrawValue& drawValue);
    void drawRegionRow(const MemoryLayout& layout, usize index, const ConfigSetting* setting,
                       const std::map<std::string, std::string, std::less<>>& values,
                       const DrawValue& drawValue);
    static void drawSettingName(const std::string& label, const ConfigSetting& setting,
                                const std::map<std::string, std::string, std::less<>>& values,
                                const std::string& reason);

    std::optional<usize> inspected;
    std::optional<usize> hovered;
    bool showsDerived = true;
};

} // namespace toki::configure
