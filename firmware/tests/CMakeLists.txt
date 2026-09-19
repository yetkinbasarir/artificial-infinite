cmake_minimum_required(VERSION 3.18)
project(pikocore_native_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(clock_sync_test
  clock_sync_test.cpp
  ../src/ClockSync.cpp
)
target_include_directories(clock_sync_test PRIVATE ../src)
target_compile_options(clock_sync_test PRIVATE -Wall -Wextra -Werror)

add_executable(tempo_engine_test
  tempo_engine_test.cpp
  ../src/TempoEngine.cpp
)
target_include_directories(tempo_engine_test PRIVATE ../src)
target_compile_options(tempo_engine_test PRIVATE -Wall -Wextra -Werror)

add_executable(dsp_test
  dsp_test.cpp
)
target_include_directories(dsp_test PRIVATE ../src ..)
target_compile_options(dsp_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME clock_sync_test COMMAND clock_sync_test)
add_test(NAME dsp_test COMMAND dsp_test)
add_test(NAME tempo_engine_test COMMAND tempo_engine_test)
