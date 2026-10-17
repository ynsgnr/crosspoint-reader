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

add_executable(OpdsSyncCoreTest
  OpdsSyncCoreTest.cpp
  ${REPO_ROOT}/lib/OpdsSync/OpdsSyncCore.cpp
  ${REPO_ROOT}/lib/OpdsSync/OpdsSeenLedger.cpp
)

target_include_directories(OpdsSyncCoreTest PRIVATE
  ${REPO_ROOT}/lib/OpdsSync
)

target_link_libraries(OpdsSyncCoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsSyncCoreTest)

add_executable(ImageFingerprintTest
  ImageFingerprintTest.cpp
)

target_include_directories(ImageFingerprintTest PRIVATE
  ${REPO_ROOT}/lib/OpdsSync
)

target_link_libraries(ImageFingerprintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImageFingerprintTest)
