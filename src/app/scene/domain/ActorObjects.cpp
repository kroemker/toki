#include "ActorObjects.h"

#include "app/scene/Types.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/GameCode.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr u8 SceneKeepSegment = 0x05;

} // namespace

std::optional<usize> ActorObjects::findObject(const std::string& name,
                                              std::vector<base::Diagnostic>& diagnostics) {
    if (const auto known = indices.find(name); known != indices.end()) {
        return known->second;
    }
    const auto started = std::chrono::steady_clock::now();
    const std::optional<usize> index = readObject(name, diagnostics);
    indices.emplace(name, index);
    actors.objectsElapsed += std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return index;
}

std::optional<usize> ActorObjects::readObject(const std::string& name,
                                              std::vector<base::Diagnostic>& diagnostics) {
    const hackeroot::Workspace& workspace = reader.getWorkspace();
    const std::optional<hackeroot::AssetDirectory> directory =
        hackeroot::findObjectDirectory(workspace.getAssets(), name);
    if (!directory) {
        diagnostics.push_back(base::makeWarning(
            {}, std::format("the checkout has no object '{}' to draw the actor from", name)));
        return std::nullopt;
    }
    const bool isKeep = name == hackeroot::KeepObjectName;
    const u8 segment = isKeep              ? hackeroot::KeepSegment
                       : name == sceneKeep ? SceneKeepSegment
                                           : hackeroot::ObjectSegment;
    const std::vector<hackeroot::SharedImageFiles> shared =
        hackeroot::makeObjectImageFiles(workspace, *directory, segment);
    std::ranges::copy(shared.back().watched, std::back_inserter(actors.watched));
    const hackeroot::ImageSet images = reader.read(shared, {});
    actors.objects.push_back(
        ActorObject{.name = name, .segment = segment, .image = images.shared.back()});
    return actors.objects.size() - 1;
}

std::optional<u32> ActorObjects::findAddress(const usize object, const std::string_view symbol,
                                             std::vector<base::Diagnostic>& diagnostics) const {
    const std::optional<u32> address = actors.objects[object].image->findAddress(symbol);
    if (!address) {
        diagnostics.push_back(
            base::makeWarning({}, std::format("'{}' is not in the image of '{}'", symbol,
                                              actors.objects[object].name)));
    }
    return address;
}

} // namespace toki::scene
