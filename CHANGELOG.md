# Changelog

## Unreleased

- **Realism scorecard on real days.** Total variation now bins each statistic into equal shares of
  the pooled sample: with equal-width bins over the pooled range, the few limit orders thousands of
  ticks from the mid put the whole bulk in one bin, so depth and volume scored about 0 between
  clearly different days (`Realism.OutliersDoNotHideAShiftFromTotalVariation`).
- **Order book models results page.** `scripts/book_models.sh` (also run by `scripts/results.sh`)
  writes docs/results/book-models.md: multi-level OFI, the Markov chain and the realism scorecard on
  five stored days. `tests/lobster_check.py` keeps the occupied prices sorted instead of sorting
  the book on every line, so it checks a full real day in about a minute instead of hours.

- **Multi-level OFI and cross-impact** (Cont, Cucuringu and Zhang 2023). `itch_mlofi` / `lobiq mlofi`:
  order flow at the top 10 levels, an integrated OFI from their first principal component, its price
  impact against the best level's out of sample, and whether other symbols' lagged OFI forecasts the
  next interval. Tests: `MultiLevelOfi.*`.
- **LOBSTER export.** `itch_lobster` / `lobiq lobster` write stored ITCH days as LOBSTER message and
  orderbook files, so the deep-learning order book benchmarks (LOBCAST, LOBFrame, DeepMarket) can
  train on free Nasdaq data. `tests/lobster_check.py` rebuilds the book from the messages alone and
  matches every orderbook line.
- **Market realism scorecard.** `itch_realism` / `lobiq realism`: LOB-Bench's statistics for two
  symbol-days, or a real day against the queue-reactive simulator, by total variation and normalised
  Wasserstein distance, with stylised facts. Tests: `Realism.*`.
- **Reinforcement learning environment.** The `agent` strategy and `rl/lobiq_env.py` give a
  Gymnasium-style `reset` / `step` over the backtester; an agent that always joins ends the day with
  `naive`'s total to the cent (`RL.AgentThatJoinsIsNaive`). Strategies now see the run's PnL
  (`View::pnl`) as a reward signal.
- **How firms work.** docs/FIRMS.md maps what HRT, Jane Street and Optiver publish (huge pages,
  FPGAs, message buses, feature engineering, synthetic market data) to LobIQ's components.

- **LobIQ.** The project is now LobIQ (limit order book intelligence): repository, published site,
  container image and release archives are named `lobiq`, and so is the command
  (`scripts/lobiq`; `hft` still works).
- **Markov chain of the book.** `itch_markov` / `hft markov`: the top of book as spread and bid-share
  states, the moves between them, and the morning's direction counts predicting the afternoon's mid
  moves, scored against always guessing one direction. The studio's Prediction tab draws the
  probabilities as a heat map and the chain as a state graph.
- **Models.** The studio's Models tab draws four classic models in closed form, every input live:
  Kyle's insider equilibrium, Glosten-Milgrom spreads with Bayesian price learning, Almgren-Chriss
  optimal execution with its frontier, and Avellaneda-Stoikov quotes. `Studio.Models` checks them
  against their known identities.
- **Portfolio frontier.** The leaderboard combines selected runs on a Markowitz frontier: expected
  daily PnL against risk for every mix, with the least-risk and best-Sharpe weights.

- **hft studio.** `hft studio` serves a local web UI (Python standard library, no build step):
  every `hft` command as a job with streamed output, a leaderboard of every backtest report on disk,
  a per-run view with the equity curve and drawdown, an order-book ladder that steps through a day,
  surveillance and execution-quality reports rendered as tables, and live paper runs with a kill
  switch. Answers loopback Host names only and refuses cross-origin or non-JSON writes; job
  arguments are checked against fixed patterns and passed as an argument list (docs/STUDIO.md).
- **Risk-adjusted metrics.** Sharpe on minute PnL, maximum drawdown and time underwater, profit
  factor, hit rate, maker share, captured spread, the probabilistic Sharpe ratio and, across the
  runs of a sweep, the deflated Sharpe ratio (docs/METHODS.md).
- **Tests.** `Studio.StubTools` (server, validation and security against stub tools) and
  `Studio.EndToEnd` (a synthetic day through the web API with the real tools: backtest totals equal
  the demos' pinned values, a 10-run sweep is deflated as one set of trials).
- **Risk statistics in the backend.** `src/backtest/metrics.hpp`: `itch_backtest` prints Sharpe,
  PSR and maximum drawdown per run, writes them with the DSR to `summary.csv` and `report.html`, and
  adds `sharpe_annual`, `psr` and `dsr` columns to sweeps, each symbol's runs deflated together.
  `Metrics.*` pins the values to the studio's Python; `Studio.EndToEnd` checks both agree on a real sweep.
- **Micro-price maker.** `strategies/microprice.cpp` quotes around Stoikov's micro-price, skewed
  against inventory, never inside the best; it reproduces `naive` fill for fill with no edge or skew.
- **Studio themes.** Dark: black, cerulean and neon red; light: ice silver, aubergine and marigold,
  every text colour at 4.5:1 contrast or more.
- **Published leaderboard.** The demos write a read-only studio snapshot next to the reports, so the
  CI-published site carries the leaderboard; pull-request runs attach the reports as an artifact.
- **Paper replays.** `hft paper [RUN|sample] [pm_exec options]` replays a recorded live session
  (the bundled 20 minutes or any live run) with any quote model, report in `$HFT_REPORTS/paper-*`;
  `hft quoters` lists the models. The studio has a Paper replay tab for it, and live runs take a quote
  model and the arbitrage switch too.
- **A usable studio before and after a build.** Strategy and quoter lists fall back to
  `studio/catalog.json` (checked against the tools in CI); each strategy gets an input per parameter
  and a venue preset; the Overview explains how to build or run the tools on Windows, in Codespaces
  or in a container; a theme button cycles auto, light and dark; tabs follow back and forward.
- **Fixes.** The studio read a refused request's body only after refusing, which reset the
  connection; two backtests started in the same second shared a report directory.

## v1.5 (2026-10-13)

Applications on real data, a security and correctness pass over every input boundary, and a
faster feed decoder: surveillance, execution-quality and point-in-time book tools; corrupt
checkpoints, session files and exchange metadata rejected instead of trusted; parse latency
halved at the median and cut by 62% at p99, with identical decisions.

Applications
- **Market surveillance.** `itch_surveil` / `hft surveil`: spoofing, layering and quote-stuffing
  candidates over every symbol of a day, ranked with their evidence (docs/results/surveillance.md).
- **Execution quality.** `itch_bestex` / `hft bestex`: a Rule 605-style report of effective, realized
  and impact spreads by size and price improvement (docs/results/execution-quality.md).
- **Point-in-time books.** `itch_book` / `hft book`: any symbol's book at any moment of a day, from the
  nearest checkpoint; `ingest` takes `CHUNK_MSGS`.
- **Applications and methods.** docs/APPLICATIONS.md maps each part to the industry systems it
  mirrors; docs/METHODS.md gives every model and estimator as implemented, with sources, code and
  tests.

Fixes
- **Checkpoint hardening.** `TickBook::load` accepted any image whose sizes matched; a corrupt one
  could crash or hang the next call. It now checks sizes before allocating and accepts only an
  image that passes a range-checked `check()` (ID table, free lists, radix links, bitmaps). New
  fuzzer `book_load_fuzz`.
- **Dashboard Host check.** The loopback HTTP server now answers only requests whose Host is
  `127.0.0.1:<port>` or `localhost:<port>`: a DNS-rebinding page could otherwise read the metrics
  and set the kill switch.
- **Strict session files.** `session.tsv` is parsed with range checks; a negative connection number
  used to wrap to 2^32 - 1 in the engine. Malformed lines reject the file with their line number.
- **Exchange metadata.** Market and event strings from the metadata API have control characters
  replaced, so a tab or newline in a slug can no longer add rows to `markets.tsv`; numeric ids out of
  range no longer go through an undefined conversion.
- **Recording gaps.** Corrupt timestamps no longer overflow the gap statistic (found by UBSan).
- **Variance estimates.** The Avellaneda-Stoikov backtest strategy decayed its variance once per call
  between mid changes, so frequent calls drove it toward zero (0.017 instead of 1 tick^2/s at 1 ms
  calls); each call now scores only its own interval. The prediction-market maker no longer clamps
  sub-millisecond intervals to 1 ms. Decisions change: the paper demo's pinned hash is now
  510bf844d3995b0b, and live recordings made before replay to different decisions.
- **Study medians.** Medians over an even count now average the middle pair (they took the upper
  one); the microstructure pages and the README figures shift slightly; the 2019 micro-price gain
  moves from 10.0% to 8.1%.
- **`itch_bestex`** read a store index through a destroyed reader on compilers without C++23
  temporary lifetime extension.

Performance
- **Feed decode.** Price changes, book levels and market listings are decoded in the exchange's
  fixed layouts with literal key matches (key words computed at compile time), falling back to the
  general parser on any difference; identical decisions. Parse p50 340 -> 175 ns, p99 1,818 ->
  691 ns, p99.9 3,921 -> 2,859 ns on a P-core (PERF R20-R22).
- **Full-day replay.** `book_replay` digests the BBO stream a word at a time: a full day on one
  thread 34.9 -> 31.8 s; `bbo_hash` values change (PERF R23).
- **Peak figures re-measured** (docs/results/max-performance.md): book p50 22.1 ns on a P-core,
  prediction-market parse p50 / p99 183 / 807 ns, a full Nasdaq day in 4.48 s at 189M messages/s.

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
- **No setup needed.** Container image `ghcr.io/yash-awasthi/lobiq` (Docker or Podman) runs the
  demos; a dev container (Codespaces) builds everything; `gen_day` writes a synthetic Nasdaq
  session; a 20-minute real recording is bundled in `data/sample`.
- **Published demo reports** on every commit to main: https://yash-awasthi.github.io/lobiq/
- **Fixes.** The arbitrage executor counted an attempt twice when a failed settlement reopened a
  completed set. The decoder read past an empty numeric field and overflowed on long integer
  prices (found by the property test under UBSan). Pool mapping counters raced when pools grew on
  several threads (found by TSan).

Hashes of every earlier recording, demo and baseline are unchanged.

## v1.0 (2026-10-11)

ITCH replay (order book, matching engine, backtester), live prediction-market feed handler and
recorder, paper execution layer (risk, order manager, ledger, simulated venue with faults,
arbitrage executor), with measured results in `docs/exec/RESULTS.md`.
