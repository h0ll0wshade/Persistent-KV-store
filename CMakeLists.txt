# Configures the C++17 library, interactive demo, tests, and optional benchmark.
cmake_minimum_required(VERSION 3.16)
project(bitcask_kv LANGUAGES CXX)

option(BITCASK_BUILD_BENCHMARKS "Build the standalone benchmark executable" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bitcask_kv
    src/background_compaction_worker.cpp
    src/compaction_manager.cpp
    src/database_lock.cpp
    src/directory_sync.cpp
    src/logfile.cpp
    src/record_codec.cpp
    src/stats_manager.cpp
    src/kvdir.cpp
    src/crud_operations.cpp
    src/log_manager.cpp
    src/recovery_manager.cpp
    src/kvstore.cpp
)
target_compile_features(bitcask_kv PUBLIC cxx_std_17)
target_include_directories(bitcask_kv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/includes)
target_compile_options(bitcask_kv PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bitcask_demo main.cpp)
target_link_libraries(bitcask_demo PRIVATE bitcask_kv)
target_compile_options(bitcask_demo PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name IN ITEMS record record_codec logfile kvdir crud_operations recovery kvstore rotation compaction durability concurrency background_compaction fold_stats)
        add_executable(test_${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE bitcask_kv)
        target_compile_options(test_${test_name} PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()
endif()

if(BITCASK_BUILD_BENCHMARKS)
    add_executable(bitcask_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(bitcask_benchmark PRIVATE bitcask_kv)
    target_compile_options(bitcask_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
