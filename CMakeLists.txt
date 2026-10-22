cmake_minimum_required(VERSION 3.24)
project(casement-plugin-mosh VERSION 0.1.0 LANGUAGES CXX)

# The MinGW toolchain file only searches its own sysroot; the cross-built
# libraries are in CMAKE_PREFIX_PATH (for example /opt/mingw64).
if(CMAKE_CROSSCOMPILING)
    list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
endif()

# With MSVC, too, the plugin is one exe without DLLs to ship: the static C
# runtime, and OpenSSL and zlib from vcpkg's x64-windows-static triplet.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/casement.cmake)
enable_testing()

# Assertions stay on in release builds: mosh checks its invariants with
# assert(), and its own builds never define NDEBUG. (Taking NDEBUG out of the
# flags, rather than adding /UNDEBUG, keeps MSVC from warning about the
# conflicting options.)
foreach(flags CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    string(REGEX REPLACE "(^| )[/-]DNDEBUG($| )" " " ${flags} "${${flags}}")
endforeach()

find_package(OpenSSL 3.0 REQUIRED)
find_package(ZLIB REQUIRED)

# mosh's client (third_party/mosh, ported to Windows) with the plugin's
# replacements for what Windows lacks (src/mosh/port), and src/mosh/client.cpp,
# the plugin's one file that talks to mosh's classes. mosh is C++11 code; it
# is built as C++17, apart from the C++20 the rest uses. Its headers stay out
# of the rest: they bring names into the global namespace.
set(MOSH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mosh/src)
add_library(mosh_headers INTERFACE)
target_include_directories(mosh_headers INTERFACE src/mosh/port)
# SYSTEM: the unit tests, which include mosh's headers, get warnings for
# their own code only.
target_include_directories(mosh_headers SYSTEM INTERFACE
    ${MOSH_DIR}/crypto
    ${MOSH_DIR}/frontend
    ${MOSH_DIR}/network
    ${MOSH_DIR}/statesync
    ${MOSH_DIR}/terminal
    ${MOSH_DIR}/util)
target_compile_features(mosh_headers INTERFACE cxx_std_17)
target_compile_definitions(mosh_headers INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
if(MSVC)
    target_compile_options(mosh_headers INTERFACE /utf-8 /EHsc /permissive-)
    # mosh uses C library functions that MSVC calls unsafe.
    target_compile_definitions(mosh_headers INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(mosh_port STATIC
    ${MOSH_DIR}/crypto/base64.cc
    ${MOSH_DIR}/crypto/crypto.cc
    ${MOSH_DIR}/crypto/ocb_internal.cc
    ${MOSH_DIR}/frontend/stmclient.cc
    ${MOSH_DIR}/frontend/terminaloverlay.cc
    ${MOSH_DIR}/network/compressor.cc
    ${MOSH_DIR}/network/network.cc
    ${MOSH_DIR}/network/transportfragment.cc
    ${MOSH_DIR}/statesync/completeterminal.cc
    ${MOSH_DIR}/statesync/user.cc
    ${MOSH_DIR}/terminal/parser.cc
    ${MOSH_DIR}/terminal/parseraction.cc
    ${MOSH_DIR}/terminal/parserstate.cc
    ${MOSH_DIR}/terminal/terminal.cc
    ${MOSH_DIR}/terminal/terminaldispatcher.cc
    ${MOSH_DIR}/terminal/terminaldisplay.cc
    ${MOSH_DIR}/terminal/terminaldisplayinit.cc
    ${MOSH_DIR}/terminal/terminalframebuffer.cc
    ${MOSH_DIR}/terminal/terminalfunctions.cc
    ${MOSH_DIR}/terminal/terminaluserinput.cc
    ${MOSH_DIR}/util/timestamp.cc
    src/mosh/port/protobuf.cpp
    src/mosh/port/unicode.cpp
    src/mosh/port/windows_port.cpp
    src/mosh/client.cpp)
target_include_directories(mosh_port PRIVATE src)
target_link_libraries(mosh_port PRIVATE mosh_headers)
if(MSVC)
    # mosh converts between integer types freely; MSVC's warnings about that
    # say nothing about this code.
    target_compile_options(mosh_port PRIVATE /W3 /wd4244 /wd4267)
else()
    target_compile_options(mosh_port PRIVATE -Wall -Wextra)
endif()
target_link_libraries(mosh_port PUBLIC OpenSSL::Crypto ZLIB::ZLIB ws2_32 bcrypt crypt32 user32 advapi32)

# The plugin's own code, as a library so the tests can use it too.
add_library(mosh_plugin STATIC
    src/mosh/bootstrap.cpp
    src/mosh/connect_scanner.cpp
    src/mosh/conpty.cpp
    src/mosh/options.cpp
    src/mosh/session.cpp
    src/mosh/ssh.cpp)
target_include_directories(mosh_plugin PUBLIC src)
target_link_libraries(mosh_plugin PUBLIC casement mosh_port ws2_32)

casement_plugin_executable(casement-mosh src/main.cpp)
target_link_libraries(casement-mosh PRIVATE mosh_plugin)
target_compile_definitions(casement-mosh PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")

casement_test(options_tests unit tests/unit/options_tests.cpp)
target_link_libraries(options_tests PRIVATE mosh_plugin)

casement_test(bootstrap_tests unit tests/unit/bootstrap_tests.cpp)
target_link_libraries(bootstrap_tests PRIVATE mosh_plugin)

casement_test(port_tests unit tests/unit/port_tests.cpp)
target_link_libraries(port_tests PRIVATE mosh_port mosh_headers casement_options)

# The end-to-end tests: the plugin under casement-sim's Casement, against a
# real mosh-server, with fake-ssh standing in for ssh.exe (see ci/e2e.sh).
add_executable(fake-ssh tests/e2e/fake-ssh.cpp)
target_include_directories(fake-ssh PRIVATE third_party)
target_link_libraries(fake-ssh PRIVATE casement_options ws2_32 shell32)

casement_test(e2e_tests e2e tests/e2e/mosh_tests.cpp)
target_link_libraries(e2e_tests PRIVATE casement_sim casement_options ws2_32)
add_dependencies(e2e_tests casement-mosh fake-ssh)
