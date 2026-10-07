#pragma once

#include "app/assets/domain/WatchedReader.h"
#include "app/object/Types.h"
#include "app/object/domain/Object.h"

namespace toki::object {

/// Reads the open object, and reads it again whenever one of its files changes.
class ObjectLoader : public assets::WatchedAssetReader<Object> {
public:
    ObjectLoader();
};

} // namespace toki::object
