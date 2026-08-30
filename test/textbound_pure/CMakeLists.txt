add_executable(TextboundPureTest
  TbGameTest.cpp
  ${REPO_ROOT}/lib/Textbound/TbGame.cpp
)

target_include_directories(TextboundPureTest PRIVATE
  ${REPO_ROOT}/lib/Textbound
)

target_link_libraries(TextboundPureTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextboundPureTest)
