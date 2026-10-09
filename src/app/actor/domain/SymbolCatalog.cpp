#include "SymbolCatalog.h"

#include "app/actor/Types.h"
#include "app/object/domain/ObjectAssets.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/ObjectFiles.h"
#include "hackeroot/Workspace.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] std::vector<object::ObjectAsset> readSymbols(hackeroot::AssetImageReader& reader,
                                                           const std::string& name) {
    const hackeroot::Workspace& workspace = reader.getWorkspace();
    const std::optional<hackeroot::AssetDirectory> directory =
        hackeroot::findObjectDirectory(workspace.getAssets(), name);
    if (!directory) {
        return {};
    }
    const u8 segment =
        name == hackeroot::KeepObjectName ? hackeroot::KeepSegment : hackeroot::ObjectSegment;
    const hackeroot::ImageSet images =
        reader.read(hackeroot::makeObjectImageFiles(workspace, *directory, segment), {});
    return object::listObjectAssets(*images.shared.back(), segment);
}

} // namespace

SymbolCatalog::SymbolCatalog() : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void SymbolCatalog::open(std::shared_ptr<const hackeroot::Workspace> opened) {
    if (opened == workspace) {
        return;
    }
    workspace = std::move(opened);
    symbols.clear();
    asked.clear();
    const std::scoped_lock lock(mutex);
    pending.clear();
    finished.clear();
}

const std::vector<object::ObjectAsset>* SymbolCatalog::findSymbols(const std::string& object) {
    if (const auto found = symbols.find(object); found != symbols.end()) {
        return &found->second;
    }
    if (workspace != nullptr && asked.insert(object).second) {
        {
            const std::scoped_lock lock(mutex);
            pending.push_back(Request{.workspace = workspace, .object = object});
        }
        wake.notify_one();
    }
    return nullptr;
}

void SymbolCatalog::update() {
    std::vector<Read> reads;
    {
        const std::scoped_lock lock(mutex);
        reads = std::exchange(finished, {});
    }
    for (Read& read : reads) {
        if (read.workspace == workspace) {
            symbols.insert_or_assign(std::move(read.object), std::move(read.symbols));
        }
    }
}

void SymbolCatalog::runWorker(const std::stop_token& stop) {
    std::optional<hackeroot::AssetImageReader> reader;
    while (!stop.stop_requested()) {
        Request request;
        {
            std::unique_lock lock(mutex);
            if (!wake.wait(lock, stop, [this] { return !pending.empty(); })) {
                return;
            }
            request = std::move(pending.front());
            pending.pop_front();
        }
        if (!reader || &reader->getWorkspace() != request.workspace.get()) {
            reader.emplace(request.workspace);
        }
        std::vector<object::ObjectAsset> assets = readSymbols(*reader, request.object);
        const std::scoped_lock lock(mutex);
        finished.push_back(Read{.workspace = std::move(request.workspace),
                                .object = std::move(request.object),
                                .symbols = std::move(assets)});
    }
}

} // namespace toki::actor
