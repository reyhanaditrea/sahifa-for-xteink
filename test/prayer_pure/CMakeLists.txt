add_executable(PrayerPureTest
  PrayerPureTest.cpp
  PrayerCalcTest.cpp
  ${REPO_ROOT}/src/prayer/PrayerPure.cpp
  ${REPO_ROOT}/src/prayer/PrayerCalc.cpp
  ${REPO_ROOT}/src/prayer/PrayerHijri.cpp
)

target_include_directories(PrayerPureTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PrayerPureTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PrayerPureTest)
