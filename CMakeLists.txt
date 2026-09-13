cmake_minimum_required(VERSION 3.28)

# IDEs frequently configure by invoking cmake directly rather than through
# CMakePresets.json, which means no toolchain file and every find_package
# failing. Fall back to vcpkg here so a bare `cmake -S . -B build` works too.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

project(toki VERSION 0.1.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "No vcpkg toolchain. Set the VCPKG_ROOT environment variable, or configure "
        "with --preset debug. See README.md.")
endif()

# vcpkg's x64-windows triplet is MSVC-ABI. A MinGW compiler configures happily
# against it and then fails at link, so say so up front rather than at link time.
if(WIN32 AND NOT MSVC)
    message(WARNING
        "Compiling on Windows with ${CMAKE_CXX_COMPILER_ID} rather than MSVC. Linking "
        "against vcpkg's MSVC-ABI packages will likely fail. Point your IDE toolchain "
        "at Visual Studio, or configure with --preset debug.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(glm CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(toki_warnings INTERFACE)
if(MSVC)
    target_compile_options(toki_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(toki_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# toki_core deliberately excludes gfx/ so the test binary never pulls in a GL
# context. See the dependency rules in CLAUDE.md.
add_library(toki_core OBJECT)
target_include_directories(toki_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(toki_core PUBLIC glm::glm PRIVATE toki_warnings)

# Console subsystem while the tool is in early development, so startup failures
# and diagnostics are visible without a log panel to put them in.
add_executable(toki)
target_link_libraries(toki PRIVATE toki_core SDL3::SDL3 imgui::imgui OpenGL::GL toki_warnings)

add_subdirectory(src)

# enable_testing() rather than include(CTest): the latter adds ~28 Nightly/
# Experimental/Continuous dashboard targets that bury the real ones in an IDE's
# target list.
enable_testing()
add_subdirectory(tests)
