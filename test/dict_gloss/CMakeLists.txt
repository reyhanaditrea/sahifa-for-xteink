add_executable(DictGlossTest
  DictGlossTest.cpp
  ${REPO_ROOT}/lib/DictGloss/DictGloss.cpp
)

target_include_directories(DictGlossTest PRIVATE
  ${REPO_ROOT}/lib/DictGloss
)

# The articles under test are Lane's, so the fixtures are half Arabic; without
# /utf-8 MSVC reads them in the console code page and they stop being the text
# the reader's dictionary actually holds.
target_compile_options(DictGlossTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(DictGlossTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictGlossTest)
