add_executable(itch_count itch_count.cpp)
target_link_libraries(itch_count PRIVATE hft_core)

add_executable(ingest ingest.cpp)
target_link_libraries(ingest PRIVATE hft_core hft_data)

add_executable(store_cat store_cat.cpp)
target_link_libraries(store_cat PRIVATE hft_core hft_data)

add_executable(book_replay book_replay.cpp)
target_link_libraries(book_replay PRIVATE hft_core hft_data)

add_executable(checkpoint checkpoint.cpp)
target_link_libraries(checkpoint PRIVATE hft_core hft_data)

add_executable(itch_dump itch_dump.cpp)
target_link_libraries(itch_dump PRIVATE hft_core)

add_executable(gen_day gen_day.cpp)
target_link_libraries(gen_day PRIVATE hft_core)

add_executable(gen_fixture gen_fixture.cpp)
target_link_libraries(gen_fixture PRIVATE hft_core zstd::libzstd)

add_executable(transcode transcode.cpp)
target_link_libraries(transcode PRIVATE hft_core hft_data)

add_executable(pm_record pm_record.cpp)
target_link_libraries(pm_record PRIVATE hft_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(pm_stats pm_stats.cpp)
target_link_libraries(pm_stats PRIVATE hft_core zstd::libzstd)

add_executable(pm_mm pm_mm.cpp)
target_link_libraries(pm_mm PRIVATE hft_core zstd::libzstd)

add_executable(pm_live pm_live.cpp ${HFT_QUOTER_SOURCES})
target_link_libraries(pm_live PRIVATE hft_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(pm_exec pm_exec.cpp ${HFT_QUOTER_SOURCES})
target_link_libraries(pm_exec PRIVATE hft_core zstd::libzstd OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(itch_backtest itch_backtest.cpp ${HFT_STRATEGY_SOURCES})
target_link_libraries(itch_backtest PRIVATE hft_core hft_data)

add_executable(pm_arb pm_arb.cpp)
target_link_libraries(pm_arb PRIVATE hft_core zstd::libzstd Threads::Threads)

add_executable(itch_study itch_study.cpp)
target_link_libraries(itch_study PRIVATE hft_core hft_data)

add_executable(itch_leadlag itch_leadlag.cpp)
target_link_libraries(itch_leadlag PRIVATE hft_core hft_data)

add_executable(itch_book itch_book.cpp)
target_link_libraries(itch_book PRIVATE hft_core hft_data)

add_executable(itch_surveil itch_surveil.cpp)
target_link_libraries(itch_surveil PRIVATE hft_core hft_data)

add_executable(itch_bestex itch_bestex.cpp)
target_link_libraries(itch_bestex PRIVATE hft_core hft_data)
