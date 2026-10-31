#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "z64/SymbolData.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace toki::scene {

class SceneModel;

inline constexpr usize HeaderCommandSize = 8;
inline constexpr usize HeaderPointerSize = 4;

/// The offset in `header` of its first command of `code`, before its
/// `SCENE_CMD_END`.
[[nodiscard]] std::optional<usize> findCommand(const z64::SymbolData& header, CommandCode code);

/// The symbol the word at `offset` of `symbol` points at, if one does.
[[nodiscard]] std::optional<std::string> findPointee(const z64::SymbolData& symbol, usize offset);

/// Inserts `size` zero bytes at `at`, moving the relocations after it.
void insertBytes(z64::SymbolData& symbol, usize at, usize size);

/// Removes `size` bytes at `at`, and the relocations in them.
void eraseBytes(z64::SymbolData& symbol, usize at, usize size);

/// Adds a command of `code` before `header`'s `SCENE_CMD_END`, its bytes zero
/// but for the ID, and returns its offset. Nothing when the header has no end.
[[nodiscard]] std::optional<usize> insertCommand(z64::SymbolData& header, CommandCode code);

/// How many words of the model's symbols point at `name`.
[[nodiscard]] usize countPointers(const SceneModel& model, std::string_view name);

/// `base`, or `base` with a number after it, whichever no symbol of the
/// model is called.
[[nodiscard]] std::string makeFreeName(const SceneModel& model, std::string_view base);

/// A header of the scene or of a room, for one value of the game's scene
/// layer: `image` indexes `SceneModel::getImages`, the scene's first, then
/// each room's.
struct HeaderSite {
    usize image = 0;
    usize layer = 0;
};

/// The header the game reads for `site`: the image's main header for layer
/// 0, and otherwise the entry of its alternate header list. A room with no
/// header of its own for the layer has its main header read, as the game
/// falls back to it; a scene does not.
[[nodiscard]] std::expected<std::string, base::Diagnostic> findHeaderName(const SceneModel& model,
                                                                          const HeaderSite& site);

/// The same, made the header of `site` alone first when another layer reads
/// it too, so an edit of it shows in no other layer. A shared main header
/// keeps its place, and the layers that point at it are given a copy.
[[nodiscard]] std::expected<std::string, base::Diagnostic> ownHeader(SceneModel& model,
                                                                     const HeaderSite& site);

} // namespace toki::scene
