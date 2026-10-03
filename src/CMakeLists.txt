add_subdirectory(base)
add_subdirectory(cparser)
add_subdirectory(makeparser)
add_subdirectory(z64)
add_subdirectory(hackeroot)
add_subdirectory(app)

target_sources(toki PRIVATE Main.cpp)
