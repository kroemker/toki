#pragma once

#include "z64/DeclarationWriter.h"

namespace cparser {
class TypeTable;
} // namespace cparser

namespace toki::scene {

/// Writes a `SceneCmd` as the `SCENE_CMD_*` macro of its command, its
/// arguments taken back out of the command's bytes: counts in decimal, lists
/// as the symbols they point at, and IDs by the enumerators `types` defines
/// for them. A command is known by the value its `SCENE_CMD_ID_*` enumerator
/// has in `types`, so HackerOoT's own commands are written as macros only
/// under a configuration that defines them. `types` must outlive the writer.
[[nodiscard]] z64::ElementWriter makeSceneCommandWriter(const cparser::TypeTable& types);

} // namespace toki::scene
