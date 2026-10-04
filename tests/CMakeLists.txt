find_package(Catch2 3 CONFIG REQUIRED)

add_executable(toki_tests
        app/core/LogTest.cpp
        app/core/SettingsTest.cpp
        app/workspace/WorkspaceServiceTest.cpp
        cparser/ExpandTest.cpp
        cparser/ExpressionTest.cpp
        cparser/MacroTableTest.cpp
        cparser/ParserTest.cpp
        cparser/PredefinesTest.cpp
        cparser/PreprocessorTest.cpp
        cparser/ScannerTest.cpp
        cparser/ScrapeTest.cpp
        cparser/TableReaderTest.cpp
        hackeroot/BuildDefinesTest.cpp
        hackeroot/CheckoutRootTest.cpp
        hackeroot/CheckoutTest.cpp
        hackeroot/ConfigurationTest.cpp
        hackeroot/FilePollerTest.cpp
        hackeroot/WorkspaceTest.cpp
        hackeroot/WslPathTest.cpp
        makeparser/MakefileTest.cpp
        makeparser/ShellWordsTest.cpp
        n64/FixedMatrixTest.cpp
        n64/MemoryTest.cpp
        n64/RspTest.cpp
        n64/SegmentTest.cpp
        n64/TextureMemoryTest.cpp
        z64/DecompDialectTest.cpp
        z64/TextureIncludesTest.cpp)
target_link_libraries(toki_tests PRIVATE toki_core Catch2::Catch2WithMain toki_warnings)

set(TOKI_HACKEROOT "" CACHE PATH
    "A HackerOoT checkout for the tests that read a real one. They skip when this is empty.")
file(TO_CMAKE_PATH "${TOKI_HACKEROOT}" TOKI_HACKEROOT_PATH)
target_compile_definitions(toki_tests PRIVATE
        TOKI_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}"
        TOKI_HACKEROOT="${TOKI_HACKEROOT_PATH}")

include(Catch)
catch_discover_tests(toki_tests)
