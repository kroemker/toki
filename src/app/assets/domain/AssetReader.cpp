#include "AssetReader.h"

#include "app/assets/domain/AssetReport.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace toki::assets {

AssetReader::AssetReader() : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void AssetReader::open(std::shared_ptr<const hackeroot::Workspace> workspace,
                       hackeroot::AssetDirectory asset) {
    {
        const std::scoped_lock lock(mutex);
        pending = Request{.workspace = std::move(workspace), .asset = std::move(asset)};
    }
    wake.notify_all();
}

std::optional<AssetReport> AssetReader::update() {
    std::optional<AssetReport> report;
    {
        const std::scoped_lock lock(mutex);
        report = std::exchange(finished, std::nullopt);
    }
    if (report) {
        reported.insert_or_assign(report->directory, countReport(*report));
    }
    return report;
}

std::optional<ReportCounts>
AssetReader::findReportCounts(const std::filesystem::path& directory) const {
    const auto found = reported.find(directory);
    if (found == reported.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::string> AssetReader::findReadingName() const {
    const std::scoped_lock lock(mutex);
    if (pending) {
        return pending->asset.name;
    }
    return reading;
}

void AssetReader::runWorker(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        std::optional<Request> request;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, stop, [this] { return pending.has_value(); });
            if (stop.stop_requested()) {
                return;
            }
            request = std::exchange(pending, std::nullopt);
            if (!request) {
                continue;
            }
            reading = request->asset.name;
        }
        AssetReport report = readAssetReport(*request->workspace, request->asset);
        const std::scoped_lock lock(mutex);
        finished = std::move(report);
        reading.reset();
    }
}

} // namespace toki::assets
