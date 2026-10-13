include(GoogleTest)

add_executable(hft_tests endian_test.cpp itch_test.cpp store_test.cpp gzip_test.cpp book_test.cpp
                         book_prop_test.cpp timing_test.cpp philox_test.cpp
                         matching_test.cpp scheduler_test.cpp
                         replay_exchange_test.cpp
                         fixture_test.cpp features_test.cpp accounting_test.cpp
                         backtest_test.cpp queue_reactive_test.cpp tick_stats_test.cpp
                         intensity_test.cpp net_test.cpp pm_test.cpp
                         tape_test.cpp spsc_test.cpp histogram_test.cpp
                         arb_test.cpp engine_test.cpp session_test.cpp pm_decoder_test.cpp
                         exec_test.cpp ledger_test.cpp risk_test.cpp oms_test.cpp config_test.cpp arb_exec_test.cpp
                         sim_venue_test.cpp report_test.cpp quoter_test.cpp pool_test.cpp microstructure_test.cpp parallel_test.cpp ${HFT_STRATEGY_SOURCES} ${HFT_QUOTER_SOURCES})
target_link_libraries(hft_tests PRIVATE hft_core hft_data GTest::gtest_main rapidcheck
                                        zstd::libzstd OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(hft_tests PRIVATE HFT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
gtest_discover_tests(hft_tests)
# RapidCheck instantiates the deprecated std::aligned_storage in user translation units.
set_source_files_properties(book_prop_test.cpp matching_test.cpp tape_test.cpp pm_test.cpp pm_decoder_test.cpp exec_test.cpp ledger_test.cpp oms_test.cpp PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)

# Sanitizer runtimes define their own operator new, which the counting allocator replaces.
if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize")
  add_executable(alloc_test alloc_test.cpp)
  target_link_libraries(alloc_test PRIVATE hft_core GTest::gtest_main)
  gtest_discover_tests(alloc_test)
endif()
add_test(NAME ItchStudy.SyntheticDays COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/study_smoke.sh $<TARGET_FILE_DIR:itch_study>)
add_test(NAME ItchLeadLag.SyntheticDay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/leadlag_smoke.sh $<TARGET_FILE_DIR:itch_leadlag>)
add_test(NAME ItchBook.CheckpointMatchesReplay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/book_query.sh $<TARGET_FILE_DIR:itch_book>)
add_test(NAME HftCli.SyntheticDay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/hft_cli.sh $<TARGET_FILE_DIR:itch_backtest>)
