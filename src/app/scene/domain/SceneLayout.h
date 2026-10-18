#pragma once

#include "app/scene/Types.h"
#include "base/Diagnostic.h"
#include "z64/DeclarationWriter.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {
class TypeTable;
} // namespace cparser

namespace toki::scene {

class SceneModel;

/// The part of a scene or room a symbol is written to, as fast64 splits them.
enum class FilePart : u8 {
    Main,
    Collision,
    Cutscene,
    ModelInfo,
    Model,
};

/// Where one symbol is written, and the feature switch it is written under
/// when only a command HackerOoT defines under that switch reaches it.
struct SymbolPlace {
    FilePart part = FilePart::Main;
    /// Which of the scene's cutscene files, for `FilePart::Cutscene`.
    usize cutscene = 0;
    std::optional<std::string> condition;
};

/// Where every symbol of a `SceneModel` is written: for each image, the place
/// of each of its symbols, in the model's order.
struct SceneLayout {
    std::vector<std::vector<SymbolPlace>> places;
    usize cutsceneCount = 0;
};

/// Places every symbol in the file of whatever reaches it first from its
/// image's header, the symbol at the start of its segment, following
/// relocations in order: what the collision header command points at goes to
/// the collision file, each cutscene to a file of its own, what the room shape
/// command points at to the model info file until a display list is reached,
/// and from there to the model file. Everything else, and what nothing
/// reaches, goes to the main file. Command IDs are read from `types`.
[[nodiscard]] SceneLayout assignSceneFiles(const SceneModel& model,
                                           const cparser::TypeTable& types);

/// What a save writes for one scene file, named as in its folder.
struct WrittenFile {
    std::string name;
    std::string text;
};

struct WrittenScene {
    std::vector<WrittenFile> files;
    /// The `.c` files of the scene's segment, then of each room's, in the
    /// order the spec lists them, without their extension.
    std::vector<std::vector<std::string>> segments;
    std::vector<base::Diagnostic> diagnostics;
};

struct SceneWriting {
    /// The scene's name, as its files start: `spot04`.
    std::string_view name;
    /// The system headers the scene's header includes.
    std::span<const std::string> includes;
    const z64::DeclarationWriter* writer = nullptr;
    /// Symbols the files that were read gave a `LENGTH_` define, which is
    /// written again with the symbol's length.
    std::set<std::string, std::less<>> lengthDefines;
    /// Other `#define` lines of the files that were read, by the symbol whose
    /// name theirs starts with, as `<texture>_WIDTH`: written again as they
    /// were, before the symbol's declaration, since game code may use them.
    std::map<std::string, std::vector<std::string>, std::less<>> symbolDefines;
    /// What a symbol no image of the scene holds is, so it can be declared.
    std::function<std::optional<z64::SymbolShape>(std::string_view)> findExternal;
};

/// The scene's files in fast64's layout: `<scene>_scene_main.c`, marked as
/// Toki's, `_scene_col.c`, `_scene_cs_<n>.c`, and for each room
/// `_room_<n>_main.c`, `_model_info.c` and `_model.c`, each file only when it
/// has symbols, the main files always. `<scene>_scene.h` includes the system
/// headers and declares the scene's symbols and every symbol outside the scene
/// they point at, and `<scene>_room_<n>.h` declares that room's.
[[nodiscard]] WrittenScene writeSceneFiles(const SceneModel& model, const SceneLayout& layout,
                                           const SceneWriting& writing);

} // namespace toki::scene
