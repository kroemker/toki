#include "CollectionCatalog.h"

#include "app/actor/domain/ValueCollection.h"
#include "base/Diagnostic.h"
#include "hackeroot/FilePoller.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::chrono::milliseconds PollInterval{500};

[[nodiscard]] const ValueCollection* findIn(const std::vector<ValueCollection>& collections,
                                            const std::string_view name) {
    const auto found = std::ranges::find(collections, name, &ValueCollection::name);
    return found == collections.end() ? nullptr : &*found;
}

} // namespace

CollectionCatalog::CollectionCatalog()
    : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void CollectionCatalog::open(std::shared_ptr<const hackeroot::Workspace> opened) {
    if (opened == workspace) {
        return;
    }
    workspace = std::move(opened);
    shared.clear();
    isSharedRead = false;
    own.clear();
    asked.clear();
    {
        const std::scoped_lock lock(mutex);
        pending.clear();
        finished.clear();
        if (workspace != nullptr) {
            pending.push_back(Request{.workspace = workspace});
        }
    }
    wake.notify_one();
}

void CollectionCatalog::ask(const std::string& actor) {
    if (workspace == nullptr || !asked.insert(actor).second) {
        return;
    }
    {
        const std::scoped_lock lock(mutex);
        pending.push_back(Request{.workspace = workspace, .actor = actor});
    }
    wake.notify_one();
}

const ValueCollection* CollectionCatalog::findCollection(const std::string_view actor,
                                                         const std::string_view name) {
    ask(std::string(actor));
    if (const auto found = own.find(actor); found != own.end()) {
        if (const ValueCollection* collection = findIn(found->second, name)) {
            return collection;
        }
    }
    return findIn(shared, name);
}

std::vector<std::string> CollectionCatalog::listCollectionNames(const std::string_view actor) {
    ask(std::string(actor));
    std::vector<std::string> names;
    if (const auto found = own.find(actor); found != own.end()) {
        for (const ValueCollection& collection : found->second) {
            names.push_back(collection.name);
        }
    }
    for (const ValueCollection& collection : shared) {
        if (!std::ranges::contains(names, collection.name)) {
            names.push_back(collection.name);
        }
    }
    return names;
}

bool CollectionCatalog::hasRead(const std::string_view actor) {
    ask(std::string(actor));
    return isSharedRead && own.contains(actor);
}

void CollectionCatalog::update() {
    std::vector<Read> reads;
    {
        const std::scoped_lock lock(mutex);
        reads = std::exchange(finished, {});
    }
    for (Read& read : reads) {
        if (read.workspace != workspace) {
            continue;
        }
        std::ranges::move(read.collections.diagnostics, std::back_inserter(diagnostics));
        if (read.actor) {
            own.insert_or_assign(std::move(*read.actor), std::move(read.collections.collections));
        } else {
            shared = std::move(read.collections.collections);
            isSharedRead = true;
        }
    }
}

std::vector<base::Diagnostic> CollectionCatalog::takeDiagnostics() {
    return std::exchange(diagnostics, {});
}

CollectionCatalog::Read CollectionCatalog::readRequest(const Request& request) {
    Read read{.workspace = request.workspace, .actor = request.actor};
    if (!request.actor) {
        read.collections = readSharedCollections(*request.workspace);
        return read;
    }
    const std::vector<hackeroot::ActorEntry>& actors = request.workspace->getTables().actors;
    const auto actor = std::ranges::find(actors, *request.actor, &hackeroot::ActorEntry::enumName);
    if (actor != actors.end() && !actor->isUnset) {
        read.collections = readActorCollections(*request.workspace, *actor);
    }
    return read;
}

void CollectionCatalog::runWorker(const std::stop_token& stop) {
    struct Watch {
        Request request;
        hackeroot::FilePoller poller;
    };

    std::vector<Watch> watched;
    while (!stop.stop_requested()) {
        std::deque<Request> requests;
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, stop, PollInterval, [this] { return !pending.empty(); });
            if (stop.stop_requested()) {
                return;
            }
            requests = std::exchange(pending, {});
        }
        if (!requests.empty()) {
            std::erase_if(watched, [&](const Watch& watch) {
                return watch.request.workspace != requests.back().workspace;
            });
        }
        for (Watch& watch : watched) {
            if (!watch.poller.findChanges().empty()) {
                requests.push_back(watch.request);
            }
        }
        for (Request& request : requests) {
            if (stop.stop_requested()) {
                return;
            }
            Read read = readRequest(request);
            std::erase_if(watched, [&](const Watch& watch) {
                return watch.request.workspace == request.workspace &&
                       watch.request.actor == request.actor;
            });
            watched.push_back(Watch{.request = std::move(request),
                                    .poller = hackeroot::FilePoller(read.collections.files)});
            const std::scoped_lock lock(mutex);
            finished.push_back(std::move(read));
        }
    }
}

} // namespace toki::actor
