#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/Pass.h"
#include "base/Diagnostic.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {
class Workspace;
}

namespace toki::actor {

/// One display list a `GetItem_Draw` function draws: which of its table
/// entry's lists, in which pass, after which `sSetupDL`, placed as the matrix
/// it is loaded with, facing the camera when that matrix's rotation is the
/// billboard's, and with segment 8 sized as the function's scroll leaves it at
/// its first frame.
struct ItemDraw {
    usize list = 0;
    Pass pass = Pass::Opaque;
    u8 setup = 0;
    PartPlacement placement;
    bool isBillboard = false;
    std::vector<TileSize> tiles;
};

/// How the game draws one get-item model, `GID_HEART_PIECE`: the display lists
/// its entry of `sDrawItemTable` names, the object the first get item drawn
/// with it loads, and what its draw function draws, in order.
struct ItemModel {
    std::string drawFunction;
    std::vector<std::string> lists;
    std::optional<std::string> object;
    std::vector<ItemDraw> draws;
};

/// Every get-item model of the checkout, by its `GetItemDrawID` constant, read
/// from `sDrawItemTable` and its draw functions in `src/code/z_draw.c` and the
/// objects of `sGetItemTable` in Player's overlay. A draw function is read for
/// its literal calls only: setup display lists, `MATRIX_FINALIZE_AND_LOAD`,
/// translations, scales and the billboard's rotation, `Gfx_TwoTexScroll` on
/// segment 8 and the table's display lists. A call it does not know is a
/// diagnostic, and the model is drawn without it.
struct ItemModels {
    std::map<std::string, ItemModel, std::less<>> models;
    std::vector<std::string> drawIds;
    std::vector<base::Diagnostic> diagnostics;
    std::vector<std::filesystem::path> files;
};

[[nodiscard]] ItemModels readItemModels(const hackeroot::Workspace& workspace);

/// The model of the get-item draw ID called `name`, if the checkout has one.
[[nodiscard]] const ItemModel* findItemModel(const ItemModels& models, std::string_view name);

/// The constant of the get-item draw ID numbered `index`, if there is one.
[[nodiscard]] std::optional<std::string> findDrawIdName(const ItemModels& models, s64 index);

} // namespace toki::actor
