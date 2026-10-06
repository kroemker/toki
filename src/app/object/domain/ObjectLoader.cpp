#include "ObjectLoader.h"

#include "app/assets/domain/WatchedAssetReader.h"
#include "app/object/domain/Object.h"

namespace toki::object {

ObjectLoader::ObjectLoader() : WatchedAssetReader(readObject, listObjectWatched) {}

} // namespace toki::object
