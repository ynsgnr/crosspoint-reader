add_executable(HomeMenuIndexTest HomeMenuIndexTest.cpp)

target_include_directories(HomeMenuIndexTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(HomeMenuIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeMenuIndexTest)
