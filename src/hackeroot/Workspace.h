#pragma once

#include "base/Diagnostic.h"
#include "cparser/Scrape.h"
#include "hackeroot/Assets.h"
#include "hackeroot/BuildDefines.h"
#include "hackeroot/Configuration.h"
#include "hackeroot/ImageCache.h"
#include "hackeroot/Tables.h"
#include "makeparser/Makefile.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <vector>

namespace cparser {
class MacroTable;
} // namespace cparser

namespace hackeroot {

/// Everything read from one checkout under one set of build settings: the
/// build's defines, the scraped headers with the configuration layered over
/// them, the ordinal tables and the asset folders. A change to the checkout or
/// to the settings is a new workspace; the old one stays valid until dropped.
///
/// The scrape lives behind a pointer because the configuration's macros point
/// at it, which is what lets a workspace move. Anything read against
/// `getMacros()` must not outlive the workspace.
class Workspace {
public:
    /// Reads the checkout at `root` with `settings` as make's command line.
    /// Only an unusable checkout is an error; everything that merely went wrong
    /// on the way is in `collectDiagnostics()`.
    [[nodiscard]] static std::expected<Workspace, base::Diagnostic>
    load(const std::filesystem::path& root, const makeparser::MakeOverrides& settings);

    [[nodiscard]] const std::filesystem::path& getRoot() const {
        return root;
    }

    [[nodiscard]] const BuildDefines& getBuildDefines() const {
        return buildDefines;
    }

    /// The macros every file of this build is read against: the configuration
    /// layered over the scraped headers.
    [[nodiscard]] const cparser::MacroTable& getMacros() const;

    [[nodiscard]] const Tables& getTables() const {
        return tables;
    }

    [[nodiscard]] const Assets& getAssets() const {
        return assets;
    }

    /// The images read from this workspace and shared by everything that reads
    /// it, on any thread. It is the one part of a workspace that changes after
    /// it is loaded, and it locks itself.
    [[nodiscard]] ImageCache& getImageCache() const {
        return *imageCache;
    }

    /// Everything that went wrong while reading, from the Makefile to the asset
    /// folders, in that order.
    [[nodiscard]] std::vector<base::Diagnostic> collectDiagnostics() const;

    /// The files whose change makes this workspace stale: the Makefile and the
    /// make files it may include, every configuration header that was read, and
    /// the ordinal tables.
    [[nodiscard]] std::vector<std::filesystem::path> listWatchedFiles() const;

private:
    Workspace() = default;

    std::filesystem::path root;
    BuildDefines buildDefines;
    std::unique_ptr<cparser::ScrapeResult> scraped;
    std::unique_ptr<Configuration> configuration;
    Tables tables;
    Assets assets;
    std::unique_ptr<ImageCache> imageCache;
};

} // namespace hackeroot
