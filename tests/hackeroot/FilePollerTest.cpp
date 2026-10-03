#include "hackeroot/FilePoller.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

/// A scratch folder under the system temp directory, emptied on construction
/// and removed on destruction.
class ScratchFolder {
public:
    explicit ScratchFolder(const std::string& name)
        : path(std::filesystem::temp_directory_path() / "toki_tests" / name) {
        std::error_code failure;
        std::filesystem::remove_all(path, failure);
        std::filesystem::create_directories(path, failure);
    }

    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder(ScratchFolder&&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;
    ScratchFolder& operator=(ScratchFolder&&) = delete;

    ~ScratchFolder() {
        std::error_code failure;
        std::filesystem::remove_all(path, failure);
    }

    [[nodiscard]] std::filesystem::path getFile(const std::string& name) const {
        return path / name;
    }

private:
    std::filesystem::path path;
};

void writeFile(const std::filesystem::path& path) {
    std::ofstream(path) << "contents\n";
}

void moveModificationTime(const std::filesystem::path& path) {
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) +
                                               std::chrono::hours(1));
}

} // namespace

TEST_CASE("an untouched file is not a change", "[hackeroot][poller]") {
    const ScratchFolder folder("poller_untouched");
    writeFile(folder.getFile("a.h"));
    hackeroot::FilePoller poller({folder.getFile("a.h")});

    REQUIRE(poller.findChanges().empty());
}

TEST_CASE("a modified file is reported once", "[hackeroot][poller]") {
    const ScratchFolder folder("poller_modified");
    writeFile(folder.getFile("a.h"));
    hackeroot::FilePoller poller({folder.getFile("a.h")});

    moveModificationTime(folder.getFile("a.h"));

    REQUIRE(poller.findChanges() == std::vector<std::filesystem::path>{folder.getFile("a.h")});
    REQUIRE(poller.findChanges().empty());
}

TEST_CASE("creating a watched file and deleting one are both changes", "[hackeroot][poller]") {
    const ScratchFolder folder("poller_existence");
    writeFile(folder.getFile("old.h"));
    hackeroot::FilePoller poller({folder.getFile("old.h"), folder.getFile("new.h")});

    writeFile(folder.getFile("new.h"));
    REQUIRE(poller.findChanges() == std::vector<std::filesystem::path>{folder.getFile("new.h")});

    std::filesystem::remove(folder.getFile("old.h"));
    REQUIRE(poller.findChanges() == std::vector<std::filesystem::path>{folder.getFile("old.h")});
}
