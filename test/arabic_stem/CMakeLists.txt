add_executable(ArabicStemTest
  ArabicStemTest.cpp
  ArabicSegmentsTest.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicStem.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicSegments.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicMorph.cpp
)

target_include_directories(ArabicStemTest PRIVATE
  ${REPO_ROOT}/lib/ArabicStem
)

# The tables and expectations are Arabic literals; without /utf-8 MSVC reads the
# source in the console code page and every one of them changes meaning.
target_compile_options(ArabicStemTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(ArabicStemTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ArabicStemTest)
