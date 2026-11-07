#include "app/core/domain/UnsavedEditsQuestion.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("the unsaved edits question names every asset and what is about to happen",
          "[core][tabs]") {
    using toki::core::makeUnsavedEditsQuestion;
    CHECK(makeUnsavedEditsQuestion({"Message 0x1234"}, "closing") ==
          "You have unsaved edits in \"Message 0x1234\". Do you want to save them before closing?");
    CHECK(makeUnsavedEditsQuestion({"A", "B", "C"}, "quitting") ==
          "You have unsaved edits in \"A\", \"B\" and \"C\". Do you want to save them before "
          "quitting?");
}
