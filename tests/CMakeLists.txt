include(GoogleTest)

add_executable(lobiq_tests endian_test.cpp itch_test.cpp store_test.cpp gzip_test.cpp book_test.cpp
                         book_prop_test.cpp timing_test.cpp philox_test.cpp
                         matching_test.cpp scheduler_test.cpp
                         replay_exchange_test.cpp
                         fixture_test.cpp features_test.cpp accounting_test.cpp
                         backtest_test.cpp queue_reactive_test.cpp tick_stats_test.cpp
                         intensity_test.cpp net_test.cpp pm_test.cpp
                         tape_test.cpp spsc_test.cpp histogram_test.cpp
                         arb_test.cpp engine_test.cpp session_test.cpp pm_decoder_test.cpp
                         exec_test.cpp ledger_test.cpp risk_test.cpp oms_test.cpp config_test.cpp arb_exec_test.cpp
                         sim_venue_test.cpp report_test.cpp quoter_test.cpp pool_test.cpp microstructure_test.cpp parallel_test.cpp surveil_test.cpp bestex_test.cpp markov_test.cpp mlofi_test.cpp lobster_test.cpp realism_test.cpp
                         garch_test.cpp hawkes_mv_test.cpp regime_test.cpp conformal_test.cpp overfit_test.cpp options_test.cpp symreg_test.cpp ${LOBIQ_STRATEGY_SOURCES} ${LOBIQ_QUOTER_SOURCES})
target_link_libraries(lobiq_tests PRIVATE lobiq_core lobiq_data GTest::gtest_main rapidcheck
                                        zstd::libzstd OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(lobiq_tests PRIVATE LOBIQ_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
gtest_discover_tests(lobiq_tests)
# RapidCheck instantiates the deprecated std::aligned_storage in user translation units.
set_source_files_properties(book_prop_test.cpp matching_test.cpp tape_test.cpp pm_test.cpp pm_decoder_test.cpp exec_test.cpp ledger_test.cpp oms_test.cpp PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)

# Sanitizer runtimes define their own operator new, which the counting allocator replaces.
if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize")
  add_executable(alloc_test alloc_test.cpp)
  target_link_libraries(alloc_test PRIVATE lobiq_core GTest::gtest_main)
  gtest_discover_tests(alloc_test)
endif()
add_test(NAME ItchStudy.SyntheticDays COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/study_smoke.sh $<TARGET_FILE_DIR:itch_study>)
add_test(NAME ItchLeadLag.SyntheticDay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/leadlag_smoke.sh $<TARGET_FILE_DIR:itch_leadlag>)
add_test(NAME Research.SyntheticDays COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/research_smoke.sh $<TARGET_FILE_DIR:itch_vol>)
set_tests_properties(Research.SyntheticDays PROPERTIES TIMEOUT 900)
add_test(NAME Options.SyntheticChain COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/options_smoke.sh $<TARGET_FILE_DIR:options> ${PROJECT_SOURCE_DIR}/data/sample/options_chain.csv)
add_test(NAME ItchBook.CheckpointMatchesReplay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/book_query.sh $<TARGET_FILE_DIR:itch_book>)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME Studio.StubTools COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/studio/test_studio.py)
  add_test(NAME Studio.EndToEnd COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/studio/test_studio.py $<TARGET_FILE_DIR:itch_backtest>)
  set_tests_properties(Studio.EndToEnd PROPERTIES TIMEOUT 900)
  add_test(NAME RL.AgentThatJoinsIsNaive COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/rl/test_env.py $<TARGET_FILE_DIR:itch_backtest>)
  add_test(NAME Lobster.ExportRebuildsTheBook COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/lobster_export.sh $<TARGET_FILE_DIR:itch_lobster>)
endif()
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
  add_test(NAME Studio.Models COMMAND ${NODE_EXECUTABLE} ${PROJECT_SOURCE_DIR}/studio/test_models.mjs)
endif()
add_test(NAME LobiqCli.SyntheticDay COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/lobiq_cli.sh $<TARGET_FILE_DIR:itch_backtest>)
