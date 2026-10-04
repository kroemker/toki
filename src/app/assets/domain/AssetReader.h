#pragma once

#include "app/assets/domain/AssetReport.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace toki::assets {

/// Opens assets on a worker thread: reading a scene's files takes the better
/// part of a second over `\\wsl.localhost`, which the UI thread must never
/// wait for. Opening another asset before the first is done replaces the
/// request that has not started yet.
///
/// Every member function is called from the UI thread.
class AssetReader {
public:
    AssetReader();
    AssetReader(const AssetReader&) = delete;
    AssetReader& operator=(const AssetReader&) = delete;
    AssetReader(AssetReader&&) = delete;
    AssetReader& operator=(AssetReader&&) = delete;
    ~AssetReader() = default;

    /// Reads `asset` against `workspace`, which the reader keeps alive until
    /// the read is done even if the UI moves on to a newer workspace.
    void open(std::shared_ptr<const hackeroot::Workspace> workspace,
              hackeroot::AssetDirectory asset);

    /// A report the worker has finished since the last call. Its counts are
    /// kept for `findReportCounts`.
    [[nodiscard]] std::optional<AssetReport> update();

    /// What the last report for the asset in `directory` counted, if it has
    /// been opened this session.
    [[nodiscard]] std::optional<ReportCounts>
    findReportCounts(const std::filesystem::path& directory) const;

    /// The asset being read or waiting to be read, if any.
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
    std::optional<AssetReport> finished;
    std::map<std::filesystem::path, ReportCounts> reported;

    std::jthread worker;
};

} // namespace toki::assets
