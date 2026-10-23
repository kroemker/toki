#pragma once

#include "app/build/Types.h"
#include "app/build/domain/BuildRunner.h"
#include "app/build/domain/BuildTarget.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Panel.h"

#include <functional>

namespace toki::build {

class BuildPanel : public core::Panel {
public:
    BuildPanel(BuildRunner& buildRunner, const core::Fonts& fonts,
               std::function<void(BuildTarget)> startBuild, std::function<bool()> hasEmulator);

    void draw() override;

private:
    void drawToolbar();
    void drawLines();
    void copyLines() const;

    BuildRunner& buildRunner;
    const core::Fonts& fonts;
    std::function<void(BuildTarget)> startBuild;
    std::function<bool()> hasEmulator;
};

} // namespace toki::build
