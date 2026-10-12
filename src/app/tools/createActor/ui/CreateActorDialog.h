#pragma once

#include "app/actor/domain/CollectionCatalog.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Fonts.h"
#include "app/tools/createActor/domain/ActorTemplate.h"
#include "app/tools/ui/EditFlow.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <future>
#include <optional>
#include <string>

namespace toki::createActor {

/// Writes a new actor into the checkout: a form for its name, category, flags,
/// object and how it is made, the files it gets as a preview, and the plan of
/// its overlay and its entries in the actor table and the spec, written once
/// reviewed. The flags and load types come from the checkout's headers, each
/// with the comment that documents it.
class CreateActorDialog : public core::Dialog {
public:
    CreateActorDialog(const workspace::WorkspaceService& workspaceService,
                      actor::CollectionCatalog& collectionCatalog, const core::Fonts& fonts);

    void draw() override;

private:
    void updateChoices();
    void drawForm(const hackeroot::Workspace& workspace);
    void drawOverlay();
    void drawActor(const hackeroot::Workspace& workspace);
    void drawCategory();
    void drawObject(const hackeroot::Workspace& workspace);
    void drawFlags();
    void drawPreview(const hackeroot::Workspace& workspace);

    const workspace::WorkspaceService& workspaceService;
    actor::CollectionCatalog& collectionCatalog;
    const core::Fonts& fonts;
    const hackeroot::Workspace* listedWorkspace = nullptr;
    std::future<std::expected<ActorChoices, std::string>> reading;
    std::optional<std::expected<ActorChoices, std::string>> choices;
    ActorDraft draft;
    std::string objectFilter;
    tools::EditFlow editFlow;
};

} // namespace toki::createActor
