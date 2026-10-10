#pragma once

#include "app/actor/domain/ValueCollection.h"
#include "base/Diagnostic.h"
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
#include <string_view>
#include <thread>
#include <vector>

namespace toki::actor {

/// The value collections of the checkout, for choice params that take their
/// values from it. The shared ones are read on a worker thread when a checkout
/// is opened, and an actor's own enumerations the first time they are asked
/// for. What was read is read again when one of its files changes, polled as
/// the workspace polls its files.
///
/// Every member function is called from the UI thread.
class CollectionCatalog {
public:
    CollectionCatalog();
    CollectionCatalog(const CollectionCatalog&) = delete;
    CollectionCatalog& operator=(const CollectionCatalog&) = delete;
    CollectionCatalog(CollectionCatalog&&) = delete;
    CollectionCatalog& operator=(CollectionCatalog&&) = delete;
    ~CollectionCatalog() = default;

    /// Reads the collections of `workspace` from now on, forgetting those of
    /// another.
    void open(std::shared_ptr<const hackeroot::Workspace> workspace);

    /// The collection `name` as the actor of enum name `actor` sees it: its
    /// own enumeration of that name, or else the shared one. Nothing while it
    /// is being read or when there is none; asking reads the actor's.
    [[nodiscard]] const ValueCollection* findCollection(std::string_view actor,
                                                        std::string_view name);

    /// The names of the collections `actor` can choose from, its own first,
    /// then the shared ones. Asking reads the actor's.
    [[nodiscard]] std::vector<std::string> listCollectionNames(std::string_view actor);

    /// Takes in what the worker has read since the last call.
    void update();

    /// What went wrong reading, since the last call.
    [[nodiscard]] std::vector<base::Diagnostic> takeDiagnostics();

private:
    struct Request {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        std::optional<std::string> actor;
    };

    struct Read {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        std::optional<std::string> actor;
        CollectionsRead collections;
    };

    void runWorker(const std::stop_token& stop);
    void ask(const std::string& actor);
    [[nodiscard]] static Read readRequest(const Request& request);

    std::shared_ptr<const hackeroot::Workspace> workspace;
    std::vector<ValueCollection> shared;
    std::map<std::string, std::vector<ValueCollection>, std::less<>> own;
    std::set<std::string, std::less<>> asked;
    std::vector<base::Diagnostic> diagnostics;

    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Request> pending;
    std::vector<Read> finished;
    std::jthread worker;
};

} // namespace toki::actor
