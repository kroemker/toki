#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace toki::scene {

/// Reads the open scene on a worker thread and reads it again whenever one of
/// its files changes, so a fast64 export shows without a restart. The files are
/// polled, because Windows sends no change notifications for a checkout on
/// `\\wsl.localhost`. Opening another scene replaces the one being watched.
///
/// Every member function is called from the UI thread.
class SceneLoader {
public:
    static constexpr std::chrono::milliseconds PollInterval{500};

    SceneLoader();
    SceneLoader(const SceneLoader&) = delete;
    SceneLoader& operator=(const SceneLoader&) = delete;
    SceneLoader(SceneLoader&&) = delete;
    SceneLoader& operator=(SceneLoader&&) = delete;
    ~SceneLoader() = default;

    /// Reads `asset` against `workspace`, which the loader keeps alive for as
    /// long as it watches the scene.
    void open(std::shared_ptr<const hackeroot::Workspace> workspace,
              hackeroot::AssetDirectory asset);

    /// A scene the worker has read since the last call, first or again.
    [[nodiscard]] std::optional<Scene> update();

    /// The scene being read, if any.
    [[nodiscard]] std::optional<std::string> findReadingName() const;

private:
    struct Request {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        hackeroot::AssetDirectory asset;
    };

    void runWorker(const std::stop_token& stop);

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Request> pending;
    std::optional<std::string> reading;
    std::optional<Scene> finished;

    std::jthread worker;
};

} // namespace toki::scene
