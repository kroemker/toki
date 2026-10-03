find_package(Catch2 3 CONFIG REQUIRED)

add_executable(toki_tests
        app/core/LogTest.cpp
        app/core/SettingsTest.cpp
        hackeroot/BuildDefinesTest.cpp
        hackeroot/CheckoutTest.cpp
        hackeroot/ConfigurationTest.cpp
        hackeroot/MakefileTest.cpp
        hackeroot/ShellWordsTest.cpp
        z64/ExpandTest.cpp
        z64/ExpressionTest.cpp
        z64/MacroTableTest.cpp
        z64/ParserTest.cpp
        z64/PredefinesTest.cpp
        z64/PreprocessorTest.cpp
        z64/ScannerTest.cpp
        z64/ScrapeTest.cpp
        z64/SegmentTest.cpp
        z64/TableReaderTest.cpp)
target_link_libraries(toki_tests PRIVATE toki_core Catch2::Catch2WithMain toki_warnings)

set(TOKI_HACKEROOT "" CACHE PATH
    "A HackerOoT checkout for the tests that read a real one. They skip when this is empty.")
file(TO_CMAKE_PATH "${TOKI_HACKEROOT}" TOKI_HACKEROOT_PATH)
target_compile_definitions(toki_tests PRIVATE
        TOKI_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}"
        TOKI_HACKEROOT="${TOKI_HACKEROOT_PATH}")

include(Catch)
catch_discover_tests(toki_tests)
