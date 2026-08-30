add_executable(NewsPureTest
  NewsDateTest.cpp
  NewsTextSinkTest.cpp
  NewsDiscoveryPureTest.cpp
  NewsArticleExtractorTest.cpp
  ${REPO_ROOT}/src/news/NewsDate.cpp
  ${REPO_ROOT}/src/news/NewsTextSink.cpp
  ${REPO_ROOT}/src/news/NewsDiscoveryPure.cpp
  ${REPO_ROOT}/src/news/NewsArticleExtractorPure.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(NewsPureTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(NewsPureTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NewsPureTest)
