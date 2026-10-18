#pragma once

#include "z64/DeclarationWriter.h"

namespace cparser {
class TypeTable;
} // namespace cparser

namespace toki::scene {

/// Writes an `ActorEntry`, a placed actor or a player entry, on one line: the
/// actor by its `ActorID` enumerator, its position in decimal, and its
/// rotation and params in hex, as binary angles and bit fields read.
/// `types` must outlive the writer.
[[nodiscard]] z64::ElementWriter makeActorEntryWriter(const cparser::TypeTable& types);

/// Writes a `TransitionActorEntry` the same way, its sides first.
[[nodiscard]] z64::ElementWriter makeTransitionActorEntryWriter(const cparser::TypeTable& types);

} // namespace toki::scene
