# Changelog

## Unreleased

- **Lead-lag and a leader-filtered maker.** `itch_leadlag` measures how related products follow
  each other (same-direction next move 59-74% against a 50% placebo, mostly 0.1 ms to 1 s later);
  the `leader` strategy plugin steps back on an adverse leader move: it significantly cuts inventory
  losses but gives up as much spread, so its net out-of-sample gain is not significant. The
  walk-forward scripts report paired bootstrap intervals. `itch_backtest --index` takes symbol names.
- **Peak numbers.** `scripts/bench_max.sh` measures turbo-on performance on one P-core, one
  E-core and the whole chip, including latency with every hardware thread busy: book p50 20.4 ns
  alone and 36.6 ns under full load on a P-core; a full Nasdaq day at about 178M messages/s
  (docs/results/max-performance.md). `NO_TURBO=0 scripts/tune.sh` sets up such a run.

## v1.2 (2026-10-12)

Bare metal, a faster book and replay, and a microstructure study tested out of sample.

- **Performance gate in CI.** Every pull request builds itself and its base and fails if the
  book, `book_replay` or `pm_live --replay` executes more than 3% more instructions (cachegrind,
  deterministic on shared runners; `scripts/perf_gate.sh`).
- **Host tooling.** `sudo scripts/tune.sh` applies the benchmark-session settings and moves driver
  IRQs off the isolated cores (iwlwifi had pinned a receive queue to each; 413 interrupts in 20 s
  under load, now 0); `bench/jitter` measures how often a core is taken away (`docs/exec/HW.md`
  H19-H20).
- **Older-regime test.** The December fit scored on a 2019 day: the depth law and micro-price
  hold; fees follow the Section 31 and TAF schedule of each trade date or the run refuses.
- **Auction uncrossing.** `book_replay` no longer flags the crossed book while an auction's
  executions are reported (SPCX's IPO on 2026-06-12, 680 ms); all eight stored days pass.

- **One command, two products.** `hft` fetches, replays, backtests and studies Nasdaq days, and
  runs, lists and reviews live paper-trading sessions; the dashboard gained a kill switch
  (POST with a custom header, so other sites cannot trigger it). Shipped as `bin/hft` in releases.

- **FPGA ITCH parser.** `fpga/itch_parser.sv` parses a BinaryFILE stream at 8 bytes per clock
  and emits one decoded message per clock, one clock after its last byte. A Verilator testbench
  checks it field by field against the C++ decoder; CI runs it.
- **Host tuning measured.** Huge pages cut book p99 about 15%; `isolcpus` under WSL2 had no
  effect; kernel bypass does not apply here (`docs/exec/HW.md` W13-W16).
- **Bare-metal Arch.** Book p99 156-159 ns (WSL2: 244-261 ns), worst case 0.33-0.37 ms
  (1-8 ms) (`docs/exec/HW.md` H13-H14). Hardware counters now count on both P- and E-cores;
  the build passes on GCC 16.
- **Study rigor and data.** Results per test day, 95% bootstrap intervals over symbols, fee
  scenarios for the 30 and 10 mil access-fee caps, an end-to-end test of `itch_study` on synthetic
  days, and `ingest_day.sh` downloading through 16 connections (the server throttles each).
- **Microstructure study.** `itch_study` reproduces Cont-Kukanov-Stoikov order flow imbalance
  (lambda = 1.01) and Stoikov's micro-price out of sample; a walk-forward maker test shows an
  imbalance filter cutting losses 66% over four test days, with the edge gone six months on (docs/results).
- **Bare-metal performance.** Tick book 5% fewer cycles per event (branchless order unlink);
  profile-guided `pm_live` parse 9% faster; all five ITCH days replay at about 11M frames/s;
  lz4 kept over zstd (docs/exec/PERF.md R11-R14).
- **Isolated cores.** CPUs 4-7 isolated: doorbell futex wake max 2.8 ms -> 52-65 us, 6-7 us with
  the waking core kept out of deep idle; uncore pinned at 4.3 GHz cuts book p50 10% and
  cross-core wake 18% (`docs/exec/HW.md` H14, H17-H18).
- **Book worst case 300 -> 40 us.** Pools grow by moving pages instead of copying them; a full
  Nasdaq day replays 9% faster (docs/exec/PERF.md R15).
- **Full-day replay 27-48% faster on 16 threads.** Workers reuse one book instead of mapping
  and unmapping pools per symbol: 114-121M messages/s (R17). Live wire to decision about 9 µs
  p50 on isolated cores; `--feed-cpus` pins feed threads (R16).

## v1.1 (2026-10-11)

Usable by others: plug in strategies, run everything without setup, see results as reports.

- **Strategy plug-ins.** Nasdaq backtester strategies register by name from `strategies/*.cpp`
  (`HFT_STRATEGY`); prediction-market quote models from `strategies/pm/*.cpp` (`HFT_QUOTER`,
  `pm_exec --quoter`). The built-in quote model re-implemented as a plug-in gives identical
  decision hashes. `scripts/new_strategy.sh` writes a working starting point. Guide:
  `docs/STRATEGIES.md`.
- **Reports.** `--report DIR` writes a self-contained HTML page (charts as inline SVG, no scripts)
  plus CSVs, for backtests and paper replays.
- **Sweeps and several symbols.** `itch_backtest --sweep key=v1,v2,...` and comma-separated
  symbols run in parallel with a ranked report and per-parameter totals; `pm_exec --sweep` does
  the same for paper replays.
- **No setup needed.** Container image `ghcr.io/yash-awasthi/hft` (Docker or Podman) runs the
  demos; a dev container (Codespaces) builds everything; `gen_day` writes a synthetic Nasdaq
  session; a 20-minute real recording is bundled in `data/sample`.
- **Published demo reports** on every commit to main: https://yash-awasthi.github.io/hft/
- **Fixes.** The arbitrage executor counted an attempt twice when a failed settlement reopened a
  completed set. The decoder read past an empty numeric field and overflowed on long integer
  prices (found by the property test under UBSan). Pool mapping counters raced when pools grew on
  several threads (found by TSan).

Hashes of every earlier recording, demo and baseline are unchanged.

## v1.0 (2026-10-11)

ITCH replay (order book, matching engine, backtester), live prediction-market feed handler and
recorder, paper execution layer (risk, order manager, ledger, simulated venue with faults,
arbitrage executor), with measured results in `docs/exec/RESULTS.md`.
