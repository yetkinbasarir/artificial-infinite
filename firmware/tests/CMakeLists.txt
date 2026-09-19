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

add_executable(transport_macro_test
  transport_macro_test.cpp
  ../src/Transport.cpp
  ../src/MacroEngine.cpp
)
target_include_directories(transport_macro_test PRIVATE ../src)
target_compile_options(transport_macro_test PRIVATE -Wall -Wextra -Werror)

add_executable(looper_test
  looper_test.cpp
  ../src/Looper.cpp
)
target_include_directories(looper_test PRIVATE ../src)
target_compile_options(looper_test PRIVATE -Wall -Wextra -Werror)

add_executable(dsp_test
  dsp_test.cpp
)
target_include_directories(dsp_test PRIVATE ../src ..)
target_compile_options(dsp_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME clock_sync_test COMMAND clock_sync_test)
add_test(NAME dsp_test COMMAND dsp_test)
add_test(NAME tempo_engine_test COMMAND tempo_engine_test)
add_test(NAME transport_macro_test COMMAND transport_macro_test)
add_test(NAME looper_test COMMAND looper_test)
