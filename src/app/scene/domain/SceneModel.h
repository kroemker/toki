#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MaterialBindings.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"
#include "z64/SymbolData.h"

#include <expected>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

struct Scene;

/// A scene as a save writes it: the symbols of the scene's image and of each
/// room's, in the order each image laid them out. Edits change symbols, and
/// what they change is seen by laying the images out again (`layOut`), so the
/// viewers and the renderer read an edited scene the way they read one from
/// the checkout.
///
/// Names are one namespace across the images, as they are to the linker, so a
/// name is never in two images.
class SceneModel {
public:
    /// The symbols of every image `scene` was read into, apart from the game's
    /// code, which is laid out against but never written.
    explicit SceneModel(const Scene& scene);

    /// The scene's symbols first, then each room's.
    [[nodiscard]] const std::vector<std::vector<z64::SymbolData>>& getImages() const {
        return images;
    }

    /// The game's code the scene's images are laid out against.
    [[nodiscard]] const std::shared_ptr<const z64::MemoryImage>& getGameCode() const {
        return gameCode;
    }

    [[nodiscard]] const z64::SymbolData* findSymbol(std::string_view name) const;

    /// Which image holds `name`.
    [[nodiscard]] std::optional<usize> findImage(std::string_view name) const;

    /// Replaces the symbol of `symbol`'s name, where it was.
    [[nodiscard]] std::expected<void, base::Diagnostic> setSymbol(z64::SymbolData symbol);

    /// Adds `symbol` to the end of `image`, in that image's segment.
    [[nodiscard]] std::expected<void, base::Diagnostic> addSymbol(usize image,
                                                                  z64::SymbolData symbol);

    /// Removes `name`, which nothing may point at any more.
    [[nodiscard]] std::expected<void, base::Diagnostic> removeSymbol(std::string_view name);

    /// What the scene's render materials make their faces, saved beside
    /// its C rather than in it.
    [[nodiscard]] const MaterialBindings& getMaterialBindings() const {
        return materialBindings;
    }

    void setMaterialBindings(MaterialBindings bindings) {
        edited = edited || bindings != materialBindings;
        materialBindings = std::move(bindings);
    }

    /// Whether anything was changed since the model was made.
    [[nodiscard]] bool isEdited() const {
        return edited;
    }

    /// The images laid out again, the game's code first, as `Scene::images`
    /// holds them.
    [[nodiscard]] std::vector<std::shared_ptr<const z64::MemoryImage>> layOut() const;

private:
    [[nodiscard]] z64::SymbolData* findMutable(std::string_view name);

    /// Shared with the scene the model was made from, which holds its images
    /// shared with what the viewport draws; the game's code never changes.
    std::shared_ptr<const z64::MemoryImage> gameCode;
    std::vector<u8> segments;
    std::vector<std::vector<z64::SymbolData>> images;
    MaterialBindings materialBindings;
    bool edited = false;
};

} // namespace toki::scene
