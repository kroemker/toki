#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

/// The objects a scene's actors are drawn from, read as the first actor that
/// names each asks for it, into `actors`' objects, with their files watched.
class ActorObjects {
public:
    ActorObjects(hackeroot::AssetImageReader& reader, std::string sceneKeep, SceneActors& actors)
        : reader(reader), sceneKeep(std::move(sceneKeep)), actors(actors) {}

    /// The index in `actors.objects` of the object called `name`, read now if
    /// it is not yet. A checkout without it is a diagnostic.
    [[nodiscard]] std::optional<usize> findObject(const std::string& name,
                                                  std::vector<base::Diagnostic>& diagnostics);

    /// Where `symbol` is in the object at `object`. One it does not have is a
    /// diagnostic.
    [[nodiscard]] std::optional<u32> findAddress(usize object, std::string_view symbol,
                                                 std::vector<base::Diagnostic>& diagnostics) const;

    [[nodiscard]] const ActorObject& getObject(const usize object) const {
        return actors.objects[object];
    }

private:
    [[nodiscard]] std::optional<usize> readObject(const std::string& name,
                                                  std::vector<base::Diagnostic>& diagnostics);

    hackeroot::AssetImageReader& reader;
    std::string sceneKeep;
    SceneActors& actors;
    std::map<std::string, std::optional<usize>, std::less<>> indices;
};

} // namespace toki::scene
