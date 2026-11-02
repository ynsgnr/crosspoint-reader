# Host build of the portable mirror layer: simulator + tests. The firmware does not use this file
# (it compiles mirror/ through PlatformIO, see platformio_mirror.ini).
cmake_minimum_required(VERSION 3.16)
project(mirror_host CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# OBJECT library: the templates register themselves from static initialisers, which an archive would drop.
add_library(mirror_core OBJECT
  core/Envelope.cpp core/RefreshPolicy.cpp core/Session.cpp
  templates/chat/ChatLayout.cpp templates/chat/ChatRenderer.cpp)
target_include_directories(mirror_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_library(mirror_sim_lib OBJECT sim/SimGfx.cpp sim/Png.cpp sim/Transcript.cpp)
target_include_directories(mirror_sim_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_executable(mirror_sim sim/main.cpp $<TARGET_OBJECTS:mirror_core> $<TARGET_OBJECTS:mirror_sim_lib>)
target_include_directories(mirror_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/generated)

add_executable(mirror_tests tests/test_main.cpp $<TARGET_OBJECTS:mirror_core> $<TARGET_OBJECTS:mirror_sim_lib>)
target_include_directories(mirror_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/generated)
target_compile_definitions(mirror_tests PRIVATE MIRROR_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden")

enable_testing()
add_test(NAME mirror_tests COMMAND mirror_tests)
