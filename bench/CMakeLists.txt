add_executable(lobiq_bench endian_bench.cpp fork_bench.cpp philox_bench.cpp exec_bench.cpp ws_bench.cpp)
target_compile_definitions(lobiq_bench PRIVATE LOBIQ_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(lobiq_bench PRIVATE lobiq_core benchmark::benchmark_main)

add_executable(book_study book_study.cpp)
target_link_libraries(book_study PRIVATE lobiq_core lobiq_data)

add_executable(wake_bench wake_bench.cpp)
target_link_libraries(wake_bench PRIVATE lobiq_core)

add_executable(jitter jitter.cpp)
target_link_libraries(jitter PRIVATE lobiq_core)
