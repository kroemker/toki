#pragma once

#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Panel.h"
#include "app/tools/createActor/domain/ActorTemplate.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <future>
#include <optional>
#include <string>

namespace toki::tools {

/// Writes a new actor into the checkout: a form for its name, category, flags,
/// object and how it is made, the files it gets as a preview, and the plan of
/// its overlay and its entries in the actor table and the spec, written once
/// reviewed. The flags and load types come from the checkout's headers, each
/// with the comment that documents it.
class CreateActorPanel : public core::Panel {
public:
    CreateActorPanel(const workspace::WorkspaceService& workspaceService,
                     actor::CollectionCatalog& collectionCatalog, const core::Fonts& fonts);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    [[nodiscard]] bool isTool() const override {
        return true;
    }

    void draw() override;

private:
    void updateChoices();
    void drawForm(const hackeroot::Workspace& workspace);
    void drawObject(const hackeroot::Workspace& workspace);
    void drawFlags();
    void drawPreview(const hackeroot::Workspace& workspace);

    const workspace::WorkspaceService& workspaceService;
    actor::CollectionCatalog& collectionCatalog;
    const core::Fonts& fonts;
    const hackeroot::Workspace* readWorkspace = nullptr;
    std::future<std::expected<ActorChoices, std::string>> reading;
    std::optional<std::expected<ActorChoices, std::string>> choices;
    ActorDraft draft;
    std::string objectFilter;
    EditFlow editFlow;
};

} // namespace toki::tools
