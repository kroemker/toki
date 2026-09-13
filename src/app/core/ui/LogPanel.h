#pragma once

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/core/domain/Settings.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Panel.h"

#include <array>
#include <vector>

namespace toki::core {

class LogPanel : public Panel {
public:
    LogPanel(Log& log, LogSettings& logSettings, const Fonts& fonts);

    void draw() override;

private:
    void drawToolbar();
    void drawEntries();
    void collectVisible();
    void copyVisible() const;

    Log& log;
    LogSettings& logSettings;
    const Fonts& fonts;

    std::vector<usize> visible;
    std::array<char, 128> filter{};
};

} // namespace toki::core
