add_executable(StarDictIndexTest
  StarDictIndexTest.cpp
  ${REPO_ROOT}/lib/StarDictIndex/StarDictIndex.cpp
)

target_include_directories(StarDictIndexTest PRIVATE
  ${REPO_ROOT}/lib/StarDictIndex
)

target_link_libraries(StarDictIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StarDictIndexTest)
