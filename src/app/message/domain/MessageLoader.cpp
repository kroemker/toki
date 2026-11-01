#include "MessageLoader.h"

#include "hackeroot/AssetImage.h"
#include "hackeroot/Workspace.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace toki::message {

MessageLoader::MessageLoader()
    : WatchedReader(
          [](hackeroot::AssetImageReader&, const MessageRequest& request) {
              return std::make_shared<const MessageCatalog>(readMessageCatalog(request.workspace));
          },
          [](const hackeroot::Workspace&, const MessageRequest&) {
              return std::vector<std::filesystem::path>();
          },
          [](const SharedCatalog& catalog) { return catalog->files; }) {}

} // namespace toki::message
