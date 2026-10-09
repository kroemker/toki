find_package(Catch2 3 CONFIG REQUIRED)

add_executable(toki_tests
        app/actor/ActorDatabaseTest.cpp
        app/actor/ActorRowTest.cpp
        app/actor/ActorStepsTest.cpp
        app/actor/DatabaseEditsTest.cpp
        app/actor/DrawStepsTest.cpp
        app/actor/FieldReadingTest.cpp
        app/actor/ActorModelCheckoutTest.cpp
        app/actor/GuessedEntryTest.cpp
        app/actor/ModelRulesTest.cpp
        app/assets/AssetReportTest.cpp
        app/build/BuildRunnerTest.cpp
        app/build/BuildTextTest.cpp
        app/core/LogTest.cpp
        app/core/SettingsSearchTest.cpp
        app/core/SettingsTest.cpp
        app/object/ObjectCheckoutTest.cpp
        app/object/ObjectTest.cpp
        app/texture/TextureCatalogCheckoutTest.cpp
        app/texture/TextureCatalogTest.cpp
        app/scene/ActorDescriptionTest.cpp
        app/scene/ActorDrawsTest.cpp
        app/scene/SceneCheckoutTest.cpp
        app/scene/SceneHeaderTest.cpp
        app/scene/SceneLoaderTest.cpp
        app/scene/SceneViewTest.cpp
        app/viewport/ViewportFrameTest.cpp
        app/workspace/WorkspaceServiceTest.cpp
        cparser/AddressTest.cpp
        cparser/CallsTest.cpp
        cparser/ExpandTest.cpp
        cparser/ExpressionTest.cpp
        cparser/MacroTableTest.cpp
        cparser/ParserTest.cpp
        cparser/PredefinesTest.cpp
        cparser/PreprocessorTest.cpp
        cparser/ScannerTest.cpp
        cparser/ScrapeTest.cpp
        cparser/TableReaderTest.cpp
        cparser/TypeParserTest.cpp
        hackeroot/AssetImageCheckoutTest.cpp
        hackeroot/AssetImageTest.cpp
        hackeroot/BuildCommandTest.cpp
        hackeroot/BuildDefinesTest.cpp
        hackeroot/CheckoutRootTest.cpp
        hackeroot/CheckoutTest.cpp
        hackeroot/ConfigurationTest.cpp
        hackeroot/FilePollerTest.cpp
        hackeroot/GbiCheckoutTest.cpp
        hackeroot/GbiTest.cpp
        hackeroot/OverlayFilesTest.cpp
        hackeroot/PreludeCheckoutTest.cpp
        hackeroot/SceneFilesTest.cpp
        hackeroot/SpecTest.cpp
        hackeroot/WorkspaceTest.cpp
        hackeroot/WslPathTest.cpp
        makeparser/MakefileTest.cpp
        makeparser/ShellWordsTest.cpp
        n64/CombinerTest.cpp
        n64/DisplayListWriterTest.cpp
        n64/FixedMatrixTest.cpp
        n64/MemoryTest.cpp
        n64/RdpTest.cpp
        n64/RspTest.cpp
        n64/SegmentTest.cpp
        n64/TextureMemoryTest.cpp
        n64/TexturePackingTest.cpp
        n64/TextureUnpackingTest.cpp
        z64/DataEncoderTest.cpp
        z64/DecompDialectTest.cpp
        z64/MemoryImageTest.cpp
        z64/PngImageTest.cpp
        z64/TextureCheckoutTest.cpp
        z64/ImageTextureTest.cpp
        z64/TextureEncoderTest.cpp
        z64/TextureIncludesTest.cpp
        z64/TextureNameTest.cpp)
target_link_libraries(toki_tests PRIVATE toki_core Catch2::Catch2WithMain toki_warnings)

set(TOKI_HACKEROOT "" CACHE PATH
    "A HackerOoT checkout for the tests that read a real one. They skip when this is empty.")
file(TO_CMAKE_PATH "${TOKI_HACKEROOT}" TOKI_HACKEROOT_PATH)
target_compile_definitions(toki_tests PRIVATE
        TOKI_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}"
        TOKI_SHIPPED_ACTORS="${CMAKE_SOURCE_DIR}/assets/actors.toml"
        TOKI_HACKEROOT="${TOKI_HACKEROOT_PATH}")

include(Catch)
catch_discover_tests(toki_tests)

# Tests that draw on a real GPU, apart from toki_tests so that binary never
# needs one. Each test skips when no GPU device can be created.
add_executable(toki_gpu_tests
        gpu/ActorRenderTest.cpp
        gpu/BlendSetupTest.cpp
        gpu/RendererTest.cpp)
target_include_directories(toki_gpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(toki_gpu_tests PRIVATE toki_core toki_gfx Catch2::Catch2WithMain toki_warnings)
target_compile_definitions(toki_gpu_tests PRIVATE
        TOKI_GPU_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}"
        TOKI_SHIPPED_ACTORS="${CMAKE_SOURCE_DIR}/assets/actors.toml"
        TOKI_HACKEROOT="${TOKI_HACKEROOT_PATH}")
add_custom_command(TARGET toki_gpu_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:toki_gpu_tests>
            $<TARGET_FILE_DIR:toki_gpu_tests>
    COMMAND_EXPAND_LISTS)
catch_discover_tests(toki_gpu_tests)
