#pragma once

#include "base/Diagnostic.h"
#include "hackeroot/Types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cparser {
class MacroTable;
} // namespace cparser

namespace hackeroot {

/// `DEFINE_SCENE(ydan_scene, g_pn_06, SCENE_DEKU_TREE, SDC_DEKU_TREE, 1, 2)`.
struct SceneEntry {
    usize id = 0;
    std::string name;
    std::string titleCard;
    std::string enumName;
    std::string drawConfig;
};

/// `DEFINE_ACTOR(En_Test, ACTOR_EN_TEST, ACTOROVL_ALLOC_NORMAL, "En_Test")`, or
/// an unset slot that keeps its ID and has only an enum name.
struct ActorEntry {
    usize id = 0;
    std::string name;
    std::string enumName;
    bool isUnset = false;
};

/// `DEFINE_OBJECT(gameplay_keep, OBJECT_GAMEPLAY_KEEP)`, or an unset slot.
struct ObjectEntry {
    usize id = 0;
    std::string name;
    std::string enumName;
    bool isUnset = false;
};

/// `DEFINE_ENTRANCE(ENTR_DEKU_TREE_0, SCENE_DEKU_TREE, 0, ...)`.
struct EntranceEntry {
    usize id = 0;
    std::string enumName;
    std::string sceneEnumName;
    std::string spawn;
};

/// The ordinal tables of a checkout. Every entry's `id` is its position in its
/// table as the build sees it, so an entry the configuration leaves out is not
/// here and every entry after it has the ID the game gives it.
struct Tables {
    std::vector<SceneEntry> scenes;
    std::vector<ActorEntry> actors;
    std::vector<ObjectEntry> objects;
    std::vector<EntranceEntry> entrances;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads `include/tables/{scene,actor,object,entrance}_table.h` under
/// `repository`, answering their conditionals against `macros`, which should be
/// the build configuration's layer. A table that cannot be read is a
/// diagnostic and an empty list; an entry with too few arguments keeps its ID
/// and gets a warning, since dropping it would move every ID after it.
[[nodiscard]] Tables readTables(const cparser::MacroTable& macros,
                                const std::filesystem::path& repository);

} // namespace hackeroot
