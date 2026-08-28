add_executable(PrayerPureTest
  PrayerPureTest.cpp
  ${REPO_ROOT}/src/prayer/PrayerPure.cpp
)

target_include_directories(PrayerPureTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PrayerPureTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PrayerPureTest)
