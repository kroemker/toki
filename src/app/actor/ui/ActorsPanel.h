#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorRow.h"
#include "app/actor/domain/CollectionCatalog.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/ui/ParamsEditor.h"
#include "app/core/ui/Panel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Workspace.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// Every actor of the actor table and whether it has an entry, and the editor
/// of one entry. Edits are written to the checkout's actor database a moment
/// after the last change, where the open scene reads them from.
class ActorsPanel : public core::Panel {
public:
    ActorsPanel(const workspace::WorkspaceService& workspaceService, DatabaseStore& databaseStore,
                SymbolCatalog& symbolCatalog, CollectionCatalog& collectionCatalog);

    /// Takes the database as it was read again.
    void setDatabase(DatabaseSnapshot read);

    /// Shows the entry of the actor of enum name `actor`.
    void openActor(const std::string& actor);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    void draw() override;

private:
    void updateLists();
    void drawList();
    void drawEditor();
    void drawEditorHeader();
    void select(const std::string& actor);
    void saveDraft();
    void deriveSelected();

    const workspace::WorkspaceService& workspaceService;
    DatabaseStore& databaseStore;
    SymbolCatalog& symbolCatalog;
    CollectionCatalog& collectionCatalog;
    std::optional<DatabaseSnapshot> snapshot;

    const hackeroot::Workspace* listedWorkspace = nullptr;
    bool areRowsStale = true;
    std::vector<ActorRow> rows;
    std::vector<std::string> objects;
    std::vector<std::string> actors;
    std::string filterText;
    RowFilter filter = RowFilter::All;

    std::optional<std::string> selected;
    std::optional<ActorEntry> draft;
    ParamsEditor paramsEditor;
    bool isDirty = false;
    double changedAt = 0.0;
};

} // namespace toki::actor
