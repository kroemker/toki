find_package(Catch2 3 CONFIG REQUIRED)

add_executable(toki_tests
        app/core/LogTest.cpp
        app/core/SettingsTest.cpp
        z64/SegmentTest.cpp)
target_link_libraries(toki_tests PRIVATE toki_core Catch2::Catch2WithMain toki_warnings)

include(Catch)
catch_discover_tests(toki_tests)
