# Changelog

## Unreleased

- **Market surveillance.** `itch_surveil` / `hft surveil`: spoofing, layering and quote-stuffing
  candidates over every symbol of a day, ranked with their evidence (docs/results/surveillance.md).
- **Execution quality.** `itch_bestex` / `hft bestex`: a Rule 605-style report of effective, realized
  and impact spreads by size and price improvement (docs/results/execution-quality.md).
- **Point-in-time books.** `itch_book` / `hft book`: any symbol's book at any moment of a day, from the
  nearest checkpoint; `ingest` takes `CHUNK_MSGS`.
- **Applications.** docs/APPLICATIONS.md maps each part to the industry systems it mirrors.
- **Checkpoint hardening.** `TickBook::load` accepted any image whose sizes matched; a corrupt one
  could crash or hang the next call. It now checks sizes before allocating and accepts only an
  image that passes a range-checked `check()` (ID table, free lists, radix links, bitmaps). New
  fuzzer `book_load_fuzz`.
- **Faster feed decode.** Price changes in the exchange's fixed layout are decoded with literal key
  matches, falling back to the general parser on any difference: 38% fewer instructions per
  message, identical results (PERF R20).
- **Methods.** docs/METHODS.md: every model and estimator as implemented, with sources, code and tests.
- **Diagnosis.** The book's last outliers traced to idle cores (HW.md H21); one hour live (PERF R19).
- **Study medians.** Medians over an even count now average the middle pair (they took the upper
  one); the microstructure pages and the README figures shift slightly (spreads, depth, volatility); the 2019
  micro-price gain from 10.0% to 8.1%.
- **Variance estimates.** The Avellaneda-Stoikov backtest strategy decayed its variance once per call
  between mid changes, so frequent calls drove it toward zero (0.017 instead of 1 tick^2/s at 1 ms
  calls); each call now scores only its own interval. The prediction-market maker no longer clamps
  sub-millisecond intervals to 1 ms. Live recordings made before replay to different decisions.

## v1.4 (2026-10-13)

Validation and diagnosis: every stored day checked against the reference book, the book's last
latency outliers traced to the hardware, and an hour of live paper trading.

- **Every day validated.** The map and tick books agree on all 94,380 symbol-days of the eight
  stores, with invariant checks on; `checkpoint --verify` passes on every symbol (STATUS).
- **Book outliers explained.** Intel PT and kernel-event matching: 90% of the remaining 5-15 us
  stalls have no kernel event and disappear when the other cores poll instead of idling; not our
  code (`docs/exec/HW.md` H21).
- **One hour live.** 710,680 events, trading thread ring to decision p99.9 26.9 us; the network
  side's tail is Wi-Fi delivery bursts (`docs/exec/PERF.md` R19).
- **Tools.** `hft leadlag`; `scripts/results.sh` regenerates every results page reproducibly; one
  `parallel_for` and one `symbols.tsv` reader replace seven copied thread pools and three readers;
  the `leader` strategy is tested against the imbalance maker it extends.

## v1.3 (2026-10-13)

A microstructure lab on the stored Nasdaq days, every result scored out of sample with its
uncertainty, plus peak-performance numbers and more testing.

- **Hardening.** End-to-end tests for `itch_leadlag` and the study's accounting identities (lead-lag
  found no moves on feeds without trading-state messages); the research tools ran clean under ASan
  and UBSan on real data; longer fuzzing found nothing.
- **VPIN.** `volume_buckets` (tested) and a VPIN section in `itch_study`: no volatility signal.
- **Intraday pattern.** Activity, spread, depth and branching ratio per half-hour window.
- **Volatility signature.** `itch_study` compares realized variance at 1-60 s with 300 s sampling.
- **Trade costs.** `itch_study` splits every execution's effective spread into realized spread and
  5 s price impact: the resting side loses 0.3-0.6 bp per share before rebates on every day.
- **Hawkes self-excitation.** `fit_hawkes` (maximum likelihood, tested on simulated processes);
  the branching ratio of mid changes is about 0.5 with sub-millisecond memory, 2019 to 2026.
- **Order lifetimes.** `itch_study` follows every order to its end: 86-94% of orders in 2025-26 are
  cancelled within 2 s (70% in 2019; about a third in Hasbrouck and Saar's 2004 sample).
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
