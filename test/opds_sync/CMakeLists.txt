add_executable(OpdsSeenLedgerTest
  OpdsSeenLedgerTest.cpp
  ${REPO_ROOT}/lib/OpdsSync/OpdsSeenLedger.cpp
)

target_include_directories(OpdsSeenLedgerTest PRIVATE
  ${REPO_ROOT}/lib/OpdsSync
)

target_link_libraries(OpdsSeenLedgerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsSeenLedgerTest)
