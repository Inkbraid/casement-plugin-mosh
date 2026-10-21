cmake_minimum_required(VERSION 3.24)
project(casement-plugin-mosh VERSION 0.1.0 LANGUAGES CXX)

# The MinGW toolchain file only searches its own sysroot; the cross-built
# libraries are in CMAKE_PREFIX_PATH (for example /opt/mingw64).
if(CMAKE_CROSSCOMPILING)
    list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
endif()

include(cmake/casement.cmake)
enable_testing()

# The plugin's own code, as a library so the tests can use it too.
add_library(mosh_plugin STATIC
    src/mosh/options.cpp)
target_include_directories(mosh_plugin PUBLIC src)
target_link_libraries(mosh_plugin PUBLIC casement)

casement_plugin_executable(casement-mosh src/main.cpp)
target_link_libraries(casement-mosh PRIVATE mosh_plugin)
target_compile_definitions(casement-mosh PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")

casement_test(options_tests unit tests/unit/options_tests.cpp)
target_link_libraries(options_tests PRIVATE mosh_plugin)
