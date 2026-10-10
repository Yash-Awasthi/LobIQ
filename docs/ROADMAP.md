# Roadmap

The system has two jobs: replay Nasdaq ITCH days as fast as the hardware allows, and run a
live paper-trading pipeline against a prediction-market exchange. Every change is measured
before and after, and every phase ends with CI green on all presets.

Target machine: i7-13650HX (Raptor Lake, 6 P-cores and 8 E-cores, AVX2, FMA, BMI2,
AVX-VNNI, GFNI; no AVX-512) under WSL2. WSL2 has no hardware performance counters, so
cycle counts come from Cachegrind and `rdtscp`.

## P0. Recorder robustness (done)

- A peer reset killed `pm_record` with SIGPIPE; the TLS client now ignores it.

## P1. Debloat (done)

- Research code, the experiment registry, the transformer and study apps moved to `archive/`, then out of the tree to the `research-archive` tag.
- Removed from the build: tlx, toml++, SQLite, nanobind, the Python CI job.

## P2. Build for the machine (done)

- `native` preset: `-O3 -march=native -flto`, combined with the profile-guided presets.
- Hot threads pinned to a P-core; E-cores left to the OS and decompression.
- Result: before/after for `book_study` and `pm_stats` on the same store.

## P3. Low-level hot paths (done)

- Prediction-market book: flat price array over the 0.001 grid, bitmap of non-empty levels,
  integer sizes, in place of two `std::map`s.
- JSON: AVX2 structural scan (quotes, backslashes, braces found 32 bytes at a time) under the
  existing reader, checked against it on the recorded data.
- Single-producer single-consumer ring between the feed thread and the trading thread.
- Log-linear latency histogram in place of HdrHistogram.
- Result: messages per second for `pm_stats` and per-message parse cost, before and after.

## P4. Live paper-trading pipeline (done)

```
 feed thread: TLS WebSocket -> parse -> ring -> trading thread: book -> strategy -> risk
                                                                   -> paper order manager
 telemetry: per-stage latency histograms, counters -> metrics endpoint + dashboard
```

- `pm_live`: subscribes to complete events, keeps books, runs the logit
  Avellaneda-Stoikov maker, and fills its paper orders against the live trade stream with
  a queue-ahead count. No real orders are sent.
- Risk: per-market and total position limits, a loss stop, a stale-feed stop.
- Telemetry: ring, parse, engine and wire-to-decision latency, message rates, reconnects,
  PnL, served over HTTP on localhost (Prometheus text, JSON, dashboard).
- Record and replay: `pm_live --replay <dir>` runs the same code on recorded hours and must
  produce identical decisions for identical input.
- Arbitrage scanner: for markets whose outcomes are mutually exclusive, report when the
  best asks sum below 1 or the best bids sum above 1, with size and duration.

## P5. Trading-thread pass (done)

- `pm_live --replay` times parse and engine per message, so changes are measured on a fixed
  recording rather than on the live market.
- Single-pass message decoder with the tape as fallback and as the reference it is tested
  against; best price kept per book side; metrics folded and rendered on the HTTP thread.
- Recorder stops instead of stalling trading when it falls behind.
- Result: README "Live paper trading"; replay wall time 2.43 s to 1.92 s.

## P6. Execution layer, paper only (done)

- Ledger, risk and kill switch, order manager, simulated venue with five core faults; makers
  and an arbitrage executor route through them; `pm_exec` for replay and paper runs.
- Result: README "Paper execution"; docs/exec/RESULTS.md.

## P7. Feed path (done)

- TLS decrypts into the WebSocket parser's buffer and each message is copied once, into the
  ring: 84.7 to 44.5 ns per message, no heap allocation (docs/exec/PERF.md R7).
- Receive time is the kernel's socket timestamp, so wire to decision starts at the wire (R8);
  `--spin-feeds` lets feed threads busy-poll (R9).

## P8. Recordings and results (done)

- `pm_live --record` ran next to the recorder over two days; makers and the arbitrage
  executor are reported per recording at four latencies in docs/exec/RESULTS.md.

The project is complete: every phase above is done and nothing is open.

## Not planned

- Live order entry: never (docs/exec DECISIONS D1). The code has no order entry and a CI gate
  keeps it that way.
- A dedicated or cloud machine (for example a bare-metal cloud instance) for hardware counters,
  futex wake latency without a hypervisor and spin-mode numbers on isolated cores:
  not available here; the laptop numbers stand as measured under WSL2.
- Miss-probability research for the arbitrage executor: would need a model estimated on some
  days and tested on later ones; a research step, outside this engineering project (D12).
- Maker rebates (VENUE F18: 15-25% of fees by category, paid daily): the simulated venue leaves
  them out, so maker PnL is understated.
- Cancel-only and closed-only market modes (VENUE F12): never seen in the recordings, so there is
  nothing to test a model against.
- System zstd, LZ4, zlib-ng and OpenSSL instead of vcpkg: the vcpkg build works in CI.
- Bloomberg, FRED, Tiingo, Quiver: daily research data with no use in this pipeline.
