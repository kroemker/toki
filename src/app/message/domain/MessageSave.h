#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"
#include "hackeroot/EditPlan.h"

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::message {

/// What `assets/text/message_data.h` holds besides its rows, and so what a
/// save writes again, loses, or cannot write at all.
struct MessageSourceScan {
    /// For each line of the file, from the first, the feature switch it
    /// stands under, empty for none.
    std::vector<std::string> switches;
    /// What a save loses: `#if`s other than the feature switches, `#define`s,
    /// and includes whose rows it writes into the file in their place.
    std::vector<std::string> warnings;
    /// Why the messages cannot be saved: rows under a feature switch that is
    /// off, which were never read.
    std::vector<std::string> refusals;
    bool isTokiFile = false;
};

/// Whether a HackerOoT feature switch is on under the checkout's configuration.
using IsSwitchOn = std::function<bool(std::string_view name)>;

[[nodiscard]] MessageSourceScan scanMessageSource(std::string_view text,
                                                  const IsSwitchOn& isSwitchOn);

/// The plan that writes `table`, the messages as edited, over the checkout's
/// `assets/text/message_data.h`, which Toki owns from then on: every row is
/// written into it, the vanilla ones it included from `extracted/` too, since
/// the game searches the table in order and finds only the first row of an ID.
/// What is written is read back before it is planned, under the checkout's
/// configuration, and must read as `table`.
[[nodiscard]] std::expected<hackeroot::EditPlan, std::string>
planMessageSave(const hackeroot::Workspace& workspace, const MessageTable& table);

} // namespace toki::message
