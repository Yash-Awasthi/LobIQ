# Status

v1.6 (2026-10-22), on bare-metal Arch Linux. Nothing is open ([docs/ROADMAP.md](docs/ROADMAP.md));
what is deliberately left out is listed there under "Not planned". Changes per version: [CHANGELOG.md](CHANGELOG.md).
The research-phase log is in
[STATUS-research.md](https://github.com/Yash-Awasthi/lobiq/blob/research-archive/archive/docs/STATUS-research.md) at the research-archive tag.

## What runs

Nothing runs on its own: no service, scheduled task or logon hook. The recorders were stopped on
2026-10-11; their recordings stay in `~/data`. To record again, start them by hand (stop with
Ctrl-C or SIGTERM):

    scripts/pm_supervise.sh ~/data/pm --cap-gb 50 --per-tag 8
    scripts/pm_live_supervise.sh ~/data/pm-live --record-cap-gb 20 --port 8088 --seconds 21600

On GitHub, CI builds and tests every pull request; on main it also publishes the container image
(`ghcr.io/yash-awasthi/lobiq`) and the demo reports (https://yash-awasthi.github.io/lobiq/); a `v*`
tag publishes a release with Linux binaries.

## Verification (2026-10-22, CI, main after hft studio and the risk statistics)

GitHub Actions on every pull request and on main: 348 tests under GCC and Clang in the debug,
release, ASan, UBSan and TSan presets (344 under the sanitizers), including `Studio.EndToEnd` with
the built tools; perf gate, security gates, FPGA simulation, fuzzers 60 s each; the container image
builds, runs the demos with their pinned totals and writes the studio snapshot. Not re-run on bare
metal: the latency figures below are unchanged by these changes, which touch reporting and add a
strategy and a UI.

## Verification (2026-10-22, bare metal, v1.6)

Release and native 366 tests, ASan, UBSan and TSan 362 each, `check_parity.sh` SAME on all 14
runs, both fault campaigns with zero illegal reports and zero residual sets, demos with their pinned
hashes (paper `510bf844d3995b0b`), CLI and security gates. The new research tools ran on the stored
days: `itch_mlofi`, `itch_markov` and `itch_realism` on five days (docs/results/book-models.md,
identical on a second run); `itch_lobster` on AAPL, MSFT and NVDA of 2025-12-10 from 00:00, every
orderbook line matched by `tests/lobster_check.py`; the RL environment through a full INTC day
(20,225 steps, 0.9 s). The realism scorecard's total variation was wrong on real days and is fixed
(CHANGELOG). `scripts/perf_gate.sh` against v1.5: book, book_replay and pm_live replay within
0.01% in instructions, so the peak figures of 2026-10-13 stand.

## Verification (2026-10-13, bare metal, v1.5)

Release 340 tests (sanitizer presets 336, ASan, UBSan and TSan run locally as well as in CI),
`check_parity.sh` SAME on all 14 runs, both fault campaigns with zero illegal reports and zero
residual sets (the dropped-fill run ends in the kill switch, as designed), demos with their pinned
hashes (paper `510bf844d3995b0b`), CLI and security gates. `checkpoint --verify` ok on all eight
stores; map and tick books give the same BBO hash on S112825 and S121025 with 0 check failures.
Fuzzers beyond CI's 60 s: book_load 1.1M inputs, oms 2.9M, tape 145k seeded with recorded
messages, no crash. Peak figures re-measured (docs/results/max-performance.md); machine defaults
restored afterwards.

## Verification (2026-10-12, bare metal, before v1.2)

Release and native 314 tests each (sanitizer presets 310, in CI under GCC and Clang),
`check_parity.sh` SAME on all 14 runs, both fault campaigns with zero illegal reports and zero
residual sets, demos, CLI and security gates, both baselines (`6facf575ad8d84bc`,
`dafd558b45dc3a1a`). TSan also run by hand on multi-threaded `book_replay` and `itch_study` over
real days: no reports.

## Every stored day validated (2026-10-13)

All eight stores (2019-10-18, 2025-11-28 to 2025-12-12, 2026-06-12): `checkpoint --verify` ok on
every symbol; `book_replay` with the reference map book and the tick book gives the same BBO hash on
all 94,380 symbol-days, with the tick book's invariant check every 10,000 messages
(`CHECK_EVERY=10000`): 0 hash mismatches, 0 check failures, 0 replay errors.

## Research tools on real data (2026-10-13)

`itch_study` (5 symbols, three days including 2019), `itch_leadlag` and `itch_backtest leader
--index` ran clean under ASan (leak checks on) and UBSan on the stored Nasdaq days; CI's sanitizer
jobs cover them only on synthetic days. clang-tidy (bugprone, performance) on the new code found
nothing beyond intended conversions. Fuzzers, 180 s each beyond CI's 60 s: itch 150M inputs, tape
4.1M, oms 2.3M, ws 5.8M, no crash, leak or timeout.

## Verification (2026-10-12, bare metal, main after the restore)

Same checks as below, all passing: native and release 306 tests each (313 after the later changes; UBSan 309), `check_parity.sh` SAME on
all 14 runs, both fault campaigns with zero illegal reports and zero residual sets (the two
dropped-fill runs end in the kill switch, as designed), demos, CLI and security gates, both
baselines. The PerfCounters test no longer skips: the PMU is present.

## Verification (2026-10-11, WSL2, main before v1.1)

- Tests: release, native and Clang 306 each (alloc_test included), asan, ubsan and tsan 302;
  one test skips without hardware performance counters. The sweeps also ran under TSan.
- `scripts/check_parity.sh`: SAME on all seven recordings. `scripts/fault_campaign.sh` and
  `scripts/arb_fault_campaign.sh`: zero illegal reports, zero residual sets. `scripts/demo/*.sh`
  and `scripts/demo/all.sh`: expected hashes and totals. `scripts/check_cli.sh`,
  `scripts/security_gates.sh`: clean.
- Fuzzers itch, tape, oms and ws, 60 s each: no crash.
- Baselines: `pm_live --replay ~/data/pm-live/20261009T160027Z` gives decision_hash
  6facf575ad8d84bc; `pm_stats ~/data/ref/pm-snap | sha256sum` starts dafd558b45dc3a1a.
- The container image builds and passes the same tests under GCC 13 (Ubuntu 24.04); the dev
  container's setup command builds and runs the demos.

Details: docs/exec/PHASES.md "Wrap-up"; measured results: docs/exec/RESULTS.md.
