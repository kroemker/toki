#include "base/Types.h"
#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "z64/TextureEncoder.h"
#include "z64/TextureIncludes.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using base::u8;
using base::usize;

namespace {

constexpr std::array<std::string_view, 8> Folders{
    "objects/gameplay_keep", "objects/object_bji",           "objects/object_kibako2",
    "textures/skyboxes",     "scenes/shops/shop1",           "scenes/overworld/spot00",
    "scenes/dungeons/ydan",  "objects/object_relay_objects",
};

std::vector<u8> readBuiltBytes(const std::filesystem::path& file) {
    std::ifstream stream(file);
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    std::vector<u8> bytes;
    usize at = text.find("0x");
    while (at != std::string::npos) {
        usize end = at + 2;
        while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end])) != 0) {
            ++end;
        }
        const std::string digits = text.substr(at + 2, end - at - 2);
        for (usize i = 0; i + 1 < digits.size(); i += 2) {
            bytes.push_back(static_cast<u8>(std::stoul(digits.substr(i, 2), nullptr, 16)));
        }
        at = text.find("0x", end);
    }
    return bytes;
}

std::optional<std::filesystem::path> findVersion(const std::filesystem::path& repository) {
    std::error_code failure;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(repository / "build", failure)) {
        if (std::filesystem::is_directory(repository / "extracted" / entry.path().filename(),
                                          failure)) {
            return entry.path().filename();
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("textures encode byte for byte as the build writes them", "[z64][textures][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }
    const std::optional<std::filesystem::path> version = findVersion(repository);
    if (!version) {
        SKIP("the checkout has no build to compare with");
    }

    const std::filesystem::path extracted = repository / "extracted" / *version / "assets";
    cparser::DirectoryCache directories;
    z64::TextureEncoder encoder(directories, {repository / "assets", extracted});
    usize compared = 0;
    std::vector<std::string> failures;
    for (const std::string_view folder : Folders) {
        const std::filesystem::path built = repository / "build" / *version / "assets" / folder;
        const std::filesystem::path source = extracted / folder;
        std::error_code failure;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(built, failure)) {
            const std::string name = entry.path().filename().string();
            const std::optional<cparser::ResolvedInclude> resolved =
                z64::resolveTextureInclude(source / name, directories);
            if (!resolved) {
                continue;
            }
            ++compared;
            const std::expected<std::vector<u8>, std::string> encoded =
                encoder.encode(resolved->path, resolved->tag);
            if (!encoded) {
                failures.push_back(std::string(folder) + "/" + name + ": " + encoded.error());
            } else if (*encoded != readBuiltBytes(entry.path())) {
                failures.push_back(std::string(folder) + "/" + name + ": bytes differ");
            }
        }
    }
    for (const std::string& failure : failures) {
        UNSCOPED_INFO(failure);
    }
    WARN(compared << " generated files compared, " << failures.size() << " differ");
    REQUIRE(compared > 100);
    REQUIRE(failures.empty());
}
