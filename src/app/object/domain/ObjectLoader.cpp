#include "ObjectLoader.h"

#include "app/assets/domain/WatchedReader.h"
#include "app/object/domain/Object.h"

namespace toki::object {

ObjectLoader::ObjectLoader() : WatchedReader(readObject, listObjectWatched) {}

} // namespace toki::object
