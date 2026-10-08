#pragma once

#include "app/actor/domain/ActorDatabase.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace toki::actor {

/// The version of `actors.toml` this build reads and writes.
inline constexpr int DatabaseFormat = 1;

/// Where the shipped actor database is installed: `actors.toml` in the assets
/// folder beside the executable.
[[nodiscard]] std::filesystem::path
makeShippedDatabasePath(const std::filesystem::path& assetDirectory);

/// Where a checkout keeps its actor database: `toki/actors.toml` under it.
[[nodiscard]] std::filesystem::path makeCheckoutDatabasePath(const std::filesystem::path& root);

/// Reads an actor database from the text of `actors.toml`. Whatever cannot be
/// read is a diagnostic naming the file, the line, the actor and the key, and
/// is left out; the rest of the file is still read. Entries are also checked
/// as `checkActorEntry` checks them.
[[nodiscard]] ActorDatabase parseActorDatabase(std::string_view text,
                                               const std::filesystem::path& file);

/// Reads `file`. One that does not exist is an empty database, not an error.
[[nodiscard]] ActorDatabase readActorDatabase(const std::filesystem::path& file);

/// The text of `actors.toml` for `database`: actors and keys in a stable
/// order, so the file diffs well, and params in hexadecimal.
[[nodiscard]] std::string formatActorDatabase(const ActorDatabase& database);

/// Writes `database` to `file`, beside it first and then over it, so a write
/// that fails leaves the file as it was.
[[nodiscard]] std::expected<void, std::string> saveActorDatabase(const ActorDatabase& database,
                                                                 const std::filesystem::path& file);

} // namespace toki::actor
