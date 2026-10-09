#pragma once

#include "app/object/domain/ObjectAssets.h"
#include "hackeroot/Workspace.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace toki::actor {

/// The symbols of the checkout's objects, for picking skeletons, animations,
/// display lists and textures by name. An object is read on a worker thread
/// the first time it is asked for, through the workspace's shared images, so
/// one the open scene has already read costs nothing.
///
/// Every member function is called from the UI thread.
class SymbolCatalog {
public:
    SymbolCatalog();
    SymbolCatalog(const SymbolCatalog&) = delete;
    SymbolCatalog& operator=(const SymbolCatalog&) = delete;
    SymbolCatalog(SymbolCatalog&&) = delete;
    SymbolCatalog& operator=(SymbolCatalog&&) = delete;
    ~SymbolCatalog() = default;

    /// Reads objects of `workspace` from now on, forgetting those of another.
    void open(std::shared_ptr<const hackeroot::Workspace> workspace);

    /// The symbols of `object` once read, or nothing while it is being read or
    /// when the checkout has no such object; asking reads it.
    [[nodiscard]] const std::vector<object::ObjectAsset>* findSymbols(const std::string& object);

    /// Takes in what the worker has read since the last call.
    void update();

private:
    struct Request {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        std::string object;
    };

    struct Read {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        std::string object;
        std::vector<object::ObjectAsset> symbols;
    };

    void runWorker(const std::stop_token& stop);

    std::shared_ptr<const hackeroot::Workspace> workspace;
    std::map<std::string, std::vector<object::ObjectAsset>, std::less<>> symbols;
    std::set<std::string, std::less<>> asked;

    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Request> pending;
    std::vector<Read> finished;
    std::jthread worker;
};

} // namespace toki::actor
