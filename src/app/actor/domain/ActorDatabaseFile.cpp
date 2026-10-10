#include "ActorDatabaseFile.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/DatabaseReader.h"
#include "base/Diagnostic.h"

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace toki::actor {

std::filesystem::path makeShippedDatabasePath(const std::filesystem::path& assetDirectory) {
    return assetDirectory / "actors.toml";
}

std::filesystem::path makeCheckoutDatabasePath(const std::filesystem::path& root) {
    return root / ".toki" / "actors.toml";
}

ActorDatabase parseActorDatabase(const std::string_view text, const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse(text, file.string());
    } catch (const toml::parse_error& error) {
        ActorDatabase failed;
        failed.diagnostics.push_back(
            base::makeError({.file = file, .line = static_cast<u32>(error.source().begin.line)},
                            std::format("cannot be read as TOML: {}", error.description())));
        return failed;
    }
    ActorDatabase database = DatabaseReader(file).read(root);
    for (const auto& [name, entry] : database.entries) {
        std::ranges::move(checkActorEntry(entry), std::back_inserter(database.problems));
    }
    return database;
}

ActorDatabase readActorDatabase(const std::filesystem::path& file) {
    std::error_code status;
    const bool isPresent = std::filesystem::exists(file, status);
    if (status) {
        ActorDatabase failed;
        failed.diagnostics.push_back(base::makeError(
            {.file = file}, std::format("cannot be looked at: {}", status.message())));
        return failed;
    }
    if (!isPresent) {
        return {};
    }
    if (!std::filesystem::is_regular_file(file, status)) {
        ActorDatabase failed;
        failed.diagnostics.push_back(base::makeError({.file = file}, "is not a file"));
        return failed;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        ActorDatabase failed;
        failed.diagnostics.push_back(base::makeError({.file = file}, "cannot be opened"));
        return failed;
    }
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    return parseActorDatabase(text, file);
}

std::expected<void, std::string> saveActorDatabase(const ActorDatabase& database,
                                                   const std::filesystem::path& file) {
    std::error_code status;
    std::filesystem::create_directories(file.parent_path(), status);
    if (status) {
        return std::unexpected(
            std::format("cannot create {}: {}", file.parent_path().string(), status.message()));
    }
    std::filesystem::path written = file;
    written += ".writing";
    {
        std::ofstream stream(written, std::ios::binary | std::ios::trunc);
        stream << formatActorDatabase(database);
        stream.close();
        if (!stream) {
            std::filesystem::remove(written, status);
            return std::unexpected(std::format("cannot write {}", written.string()));
        }
    }
    std::filesystem::rename(written, file, status);
    if (status) {
        std::error_code ignored;
        std::filesystem::remove(written, ignored);
        return std::unexpected(
            std::format("cannot replace {}: {}", file.string(), status.message()));
    }
    return {};
}

} // namespace toki::actor
