add_executable(itch_count itch_count.cpp)
target_link_libraries(itch_count PRIVATE lobiq_core)

add_executable(ingest ingest.cpp)
target_link_libraries(ingest PRIVATE lobiq_core lobiq_data)

add_executable(store_cat store_cat.cpp)
target_link_libraries(store_cat PRIVATE lobiq_core lobiq_data)

add_executable(book_replay book_replay.cpp)
target_link_libraries(book_replay PRIVATE lobiq_core lobiq_data)

add_executable(checkpoint checkpoint.cpp)
target_link_libraries(checkpoint PRIVATE lobiq_core lobiq_data)

add_executable(itch_dump itch_dump.cpp)
target_link_libraries(itch_dump PRIVATE lobiq_core)

add_executable(gen_day gen_day.cpp)
target_link_libraries(gen_day PRIVATE lobiq_core)

add_executable(gen_fixture gen_fixture.cpp)
target_link_libraries(gen_fixture PRIVATE lobiq_core zstd::libzstd)

add_executable(transcode transcode.cpp)
target_link_libraries(transcode PRIVATE lobiq_core lobiq_data)

add_executable(pm_record pm_record.cpp)
target_link_libraries(pm_record PRIVATE lobiq_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(pm_stats pm_stats.cpp)
target_link_libraries(pm_stats PRIVATE lobiq_core zstd::libzstd)

add_executable(pm_mm pm_mm.cpp)
target_link_libraries(pm_mm PRIVATE lobiq_core zstd::libzstd)

add_executable(pm_live pm_live.cpp ${LOBIQ_QUOTER_SOURCES})
target_link_libraries(pm_live PRIVATE lobiq_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(pm_exec pm_exec.cpp ${LOBIQ_QUOTER_SOURCES})
target_link_libraries(pm_exec PRIVATE lobiq_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(itch_backtest itch_backtest.cpp ${LOBIQ_STRATEGY_SOURCES})
target_link_libraries(itch_backtest PRIVATE lobiq_core lobiq_data)

add_executable(pm_arb pm_arb.cpp)
target_link_libraries(pm_arb PRIVATE lobiq_core zstd::libzstd Threads::Threads)

add_executable(itch_study itch_study.cpp)
target_link_libraries(itch_study PRIVATE lobiq_core lobiq_data)

add_executable(itch_leadlag itch_leadlag.cpp)
target_link_libraries(itch_leadlag PRIVATE lobiq_core lobiq_data)

add_executable(itch_book itch_book.cpp)
target_link_libraries(itch_book PRIVATE lobiq_core lobiq_data)

add_executable(itch_markov itch_markov.cpp)
target_link_libraries(itch_markov PRIVATE lobiq_core lobiq_data)

add_executable(itch_mlofi itch_mlofi.cpp)
target_link_libraries(itch_mlofi PRIVATE lobiq_core lobiq_data)

add_executable(itch_lobster itch_lobster.cpp)
target_link_libraries(itch_lobster PRIVATE lobiq_core lobiq_data)

add_executable(itch_realism itch_realism.cpp)
target_link_libraries(itch_realism PRIVATE lobiq_core lobiq_data)

add_executable(itch_surveil itch_surveil.cpp)
target_link_libraries(itch_surveil PRIVATE lobiq_core lobiq_data)

add_executable(itch_bestex itch_bestex.cpp)
target_link_libraries(itch_bestex PRIVATE lobiq_core lobiq_data)

add_executable(itch_vol itch_vol.cpp)
target_link_libraries(itch_vol PRIVATE lobiq_core lobiq_data)

add_executable(itch_hawkes itch_hawkes.cpp)
target_link_libraries(itch_hawkes PRIVATE lobiq_core lobiq_data)

add_executable(itch_regime itch_regime.cpp)
target_link_libraries(itch_regime PRIVATE lobiq_core lobiq_data)

add_executable(itch_alpha itch_alpha.cpp)
target_link_libraries(itch_alpha PRIVATE lobiq_core lobiq_data)

add_executable(options options.cpp)
target_link_libraries(options PRIVATE lobiq_core)
