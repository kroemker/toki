#include "cparser/Predefines.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("-D and -U become the directives they stand for", "[cparser][predefines]") {
    const std::vector<std::string> arguments = {"-DDEBUG_FEATURES=1", "-DCOMPILER_GCC", "-UNDEBUG",
                                                R"(-DNAME="a b")"};

    REQUIRE(cparser::buildPredefineSource(arguments) == "#define DEBUG_FEATURES 1\n"
                                                        "#define COMPILER_GCC 1\n"
                                                        "#undef NDEBUG\n"
                                                        "#define NAME \"a b\"\n");
}

TEST_CASE("-std= defines the language version and nothing else is read", "[cparser][predefines]") {
    const std::vector<std::string> arguments = {"-std=gnu11", "-O2", "-G", "0", "-std=madeup"};

    REQUIRE(cparser::buildPredefineSource(arguments) == "#define __STDC_VERSION__ 201112L\n");
}
