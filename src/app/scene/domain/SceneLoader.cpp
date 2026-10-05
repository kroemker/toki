#include "SceneLoader.h"

#include "app/scene/domain/Scene.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/FilePoller.h"
#include "hackeroot/SceneFiles.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

SceneLoader::SceneLoader() : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void SceneLoader::open(std::shared_ptr<const hackeroot::Workspace> workspace,
                       hackeroot::AssetDirectory asset) {
    {
        const std::scoped_lock lock(mutex);
        pending = Request{.workspace = std::move(workspace), .asset = std::move(asset)};
    }
    wake.notify_all();
}

std::optional<Scene> SceneLoader::update() {
    const std::scoped_lock lock(mutex);
    return std::exchange(finished, std::nullopt);
}

std::optional<std::string> SceneLoader::findReadingName() const {
    const std::scoped_lock lock(mutex);
    if (pending) {
        return pending->asset.name;
    }
    return reading;
}

void SceneLoader::runWorker(const std::stop_token& stop) {
    std::optional<Request> watching;
    std::optional<hackeroot::FilePoller> poller;
    std::optional<hackeroot::AssetImageReader> reader;
    while (!stop.stop_requested()) {
        std::optional<Request> request;
        {
            std::unique_lock lock(mutex);
            const bool woken =
                wake.wait_for(lock, stop, PollInterval, [this] { return pending.has_value(); });
            if (stop.stop_requested()) {
                return;
            }
            if (woken) {
                request = std::exchange(pending, std::nullopt);
            }
        }
        if (!request) {
            if (!watching || !poller || poller->findChanges().empty()) {
                continue;
            }
            request = watching;
        }
        {
            const std::scoped_lock lock(mutex);
            reading = request->asset.name;
        }
        std::vector<std::filesystem::path> watched =
            hackeroot::findSceneFiles(request->asset).getAllFiles();
        watched.push_back(request->asset.directory);
        poller.emplace(std::move(watched));
        if (!reader || &reader->getWorkspace() != request->workspace.get()) {
            reader.emplace(request->workspace);
        }
        Scene scene = readScene(*reader, request->asset);
        watching = std::move(request);

        const std::scoped_lock lock(mutex);
        finished = std::move(scene);
        reading.reset();
    }
}

} // namespace toki::scene
