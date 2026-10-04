#include "BuildLog.h"

#include "app/build/Types.h"

#include <algorithm>
#include <string>
#include <utility>

namespace toki::build {

BuildLog::BuildLog(const usize maxLines) : maxLines(std::max<usize>(maxLines, 1)) {}

void BuildLog::append(std::string line) {
    lines.push_back(std::move(line));
    while (lines.size() > maxLines) {
        lines.pop_front();
    }
}

void BuildLog::clear() {
    lines.clear();
}

} // namespace toki::build
