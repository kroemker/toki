#include "hackeroot/OverlayFiles.h"

#include "base/Diagnostic.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace {

/// A checkout in the temp directory with one actor's overlay folder.
class OverlayCheckout {
public:
    OverlayCheckout()
        : root(std::filesystem::temp_directory_path() / "toki_tests" / "overlay_files") {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
        const std::filesystem::path folder = root / "src/overlays/actors/ovl_En_Test_Two";
        std::filesystem::create_directories(folder, failure);
        for (const char* name : {"z_en_test_two.c", "z_en_test_two.h", "z_en_test_two_data.c"}) {
            std::ofstream(folder / name) << "";
        }
    }

    OverlayCheckout(const OverlayCheckout&) = delete;
    OverlayCheckout(OverlayCheckout&&) = delete;
    OverlayCheckout& operator=(const OverlayCheckout&) = delete;
    OverlayCheckout& operator=(OverlayCheckout&&) = delete;

    ~OverlayCheckout() {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
    }

    std::filesystem::path root;
};

} // namespace

TEST_CASE("an actor's overlay is the file named after it, with the folder's headers",
          "[hackeroot][overlay]") {
    const OverlayCheckout checkout;

    const std::expected<hackeroot::OverlayFiles, base::Diagnostic> overlay =
        hackeroot::findOverlayFiles(checkout.root, "En_Test_Two");

    REQUIRE(overlay.has_value());
    REQUIRE(overlay->main.filename() == "z_en_test_two.c");
    REQUIRE(overlay->headers.size() == 1);
    REQUIRE(overlay->headers.front().filename() == "z_en_test_two.h");
    REQUIRE(overlay->getAllFiles().size() == 2);
}

TEST_CASE("an actor without an overlay folder says where it looked", "[hackeroot][overlay]") {
    const OverlayCheckout checkout;

    const std::expected<hackeroot::OverlayFiles, base::Diagnostic> overlay =
        hackeroot::findOverlayFiles(checkout.root, "Player");

    REQUIRE_FALSE(overlay.has_value());
    REQUIRE(overlay.error().message.contains("ovl_Player"));
}
