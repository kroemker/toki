#pragma once

#include "app/assets/domain/WatchedReader.h"
#include "app/message/domain/MessageCatalog.h"

#include <memory>
#include <string>

namespace toki::message {

/// What the messages are read for: the checkout, kept alive by the catalog
/// read from it, and a name to say what is being read.
struct MessageRequest {
    std::string name;
    std::shared_ptr<const hackeroot::Workspace> workspace;
};

// Shared between the Assets panel's Messages tab and the Message panel, which
// both read it; a catalog is replaced whole, never changed.
using SharedCatalog = std::shared_ptr<const MessageCatalog>;

/// Reads the checkout's message catalog on a worker of its own, and again
/// whenever a file it was read from changes.
class MessageLoader : public assets::WatchedReader<MessageRequest, SharedCatalog> {
public:
    MessageLoader();
};

} // namespace toki::message
