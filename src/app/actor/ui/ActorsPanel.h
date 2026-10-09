#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorRow.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/core/ui/Panel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Workspace.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// Every actor of the actor table with where its entry is and how sure, and
/// the editor of one entry. Edits are written to the checkout's actor
/// database a moment after the last change, where the open scene reads them
/// from; editing a shipped entry starts the checkout's own from it.
class ActorsPanel : public core::Panel {
public:
    ActorsPanel(const workspace::WorkspaceService& workspaceService, DatabaseStore& databaseStore,
                SymbolCatalog& symbolCatalog);

    /// Takes both databases as they were read again.
    void setDatabases(DatabaseSnapshot databases);

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

    const workspace::WorkspaceService& workspaceService;
    DatabaseStore& databaseStore;
    SymbolCatalog& symbolCatalog;
    std::optional<DatabaseSnapshot> databases;

    const hackeroot::Workspace* listedWorkspace = nullptr;
    bool areRowsStale = true;
    std::vector<ActorRow> rows;
    std::vector<std::string> objects;
    std::vector<std::string> actors;
    std::string filterText;
    RowFilter filter = RowFilter::All;

    std::optional<std::string> selected;
    std::optional<EntryOrigin> draftOrigin;
    std::optional<ActorEntry> draft;
    bool isDirty = false;
    double changedAt = 0.0;
};

} // namespace toki::actor
