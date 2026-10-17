include(FetchContent)

FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
)
FetchContent_MakeAvailable(ArduinoJson)

add_executable(TrmnlProtocolTest
  TrmnlProtocolTest.cpp
  ${REPO_ROOT}/lib/Trmnl/TrmnlProtocol.cpp
)

target_include_directories(TrmnlProtocolTest PRIVATE
  ${REPO_ROOT}/lib/Trmnl
)

target_link_libraries(TrmnlProtocolTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(TrmnlProtocolTest)
