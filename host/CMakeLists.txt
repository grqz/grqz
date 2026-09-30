cmake_minimum_required(VERSION 3.16)
project(neondl_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
  # Static runtime: one small exe with no DLL dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_executable(neondl-host src/main_win.cpp)
  target_link_libraries(neondl-host PRIVATE ole32 shell32 uuid)
  if(MSVC)
    target_compile_options(neondl-host PRIVATE /W4 /O1 /utf-8)
    target_link_options(neondl-host PRIVATE /OPT:REF /OPT:ICF)
  else()
    target_compile_options(neondl-host PRIVATE -Wall -Wextra -Os)
    target_link_options(neondl-host PRIVATE -static -s)
  endif()
endif()

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_include_directories(core_tests PRIVATE src)
add_test(NAME core_tests COMMAND core_tests)
