#pragma once

#include "app/build/Types.h"

#include <deque>
#include <string>

namespace toki::build {

/// The output of the builds run this session, line by line. Old lines are
/// dropped past `maxLines`, so a long session cannot grow it without bound.
class BuildLog {
public:
    explicit BuildLog(usize maxLines = 50'000);

    void append(std::string line);

    void clear();

    [[nodiscard]] const std::deque<std::string>& getLines() const {
        return lines;
    }

private:
    usize maxLines;
    std::deque<std::string> lines;
};

} // namespace toki::build
