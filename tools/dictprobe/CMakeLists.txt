# Built by the host test CMake (test/CMakeLists.txt) rather than PlatformIO:
# this is a desktop tool, not firmware.
add_executable(dictprobe
  dictprobe.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicStem.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicSegments.cpp
  ${REPO_ROOT}/lib/ArabicStem/ArabicMorph.cpp
  ${REPO_ROOT}/lib/StarDictIndex/StarDictIndex.cpp
)

target_include_directories(dictprobe PRIVATE
  ${REPO_ROOT}/lib/ArabicStem
  ${REPO_ROOT}/lib/StarDictIndex
)

target_compile_options(dictprobe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# fopen is the portable spelling and the file is opened read-only.
target_compile_definitions(dictprobe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

target_link_libraries(dictprobe PRIVATE crosspoint_test_common)
