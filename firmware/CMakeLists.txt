cmake_minimum_required(VERSION 3.20)        # oldest CMake this file supports
project(pan_tracker_fw LANGUAGES C)         # project name; C only for now

set(CMAKE_C_STANDARD 11)                    # build as C11
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Library: portable code (host and MCU) ---
add_library(pid lib/pid.c)                  # a target named "pid" built from pid.c
target_include_directories(pid PUBLIC lib)  # anyone linking "pid" can #include "pid.h"
target_compile_options(pid PRIVATE -Wall -Wextra -Wpedantic)
# isfinite() is a macro, but some platforms still need libm:
if(NOT APPLE)
  target_link_libraries(pid PUBLIC m)
endif()

add_library(cli lib/cli.c)                  # command line core (parser, line editing)
target_include_directories(cli PUBLIC lib)
target_compile_options(cli PRIVATE -Wall -Wextra -Wpedantic)

add_library(led_fade lib/led_fade.c)        # LED breathing curve
target_include_directories(led_fade PUBLIC lib)
target_compile_options(led_fade PRIVATE -Wall -Wextra -Wpedantic)

# --- Tests: host only ---
include(CTest)                              # enables testing + ctest
include(FetchContent)                       # lets CMake download Unity
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.6.0                            # pin a version so builds are repeatable
)
FetchContent_MakeAvailable(unity)           # creates a target named "unity"
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)   # enable TEST_ASSERT_DOUBLE_*

add_executable(test_pid tests/test_pid.c)   # the test program
target_link_libraries(test_pid PRIVATE pid unity)
add_test(NAME test_pid COMMAND test_pid)    # register it with ctest

add_executable(test_cli tests/test_cli.c)
target_link_libraries(test_cli PRIVATE cli unity)
add_test(NAME test_cli COMMAND test_cli)

add_executable(test_led_fade tests/test_led_fade.c)
target_link_libraries(test_led_fade PRIVATE led_fade unity)
add_test(NAME test_led_fade COMMAND test_led_fade)
