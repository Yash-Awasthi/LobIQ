# Execution layer: index

Read order: INDEX -> DECISIONS -> PHASES (current step) -> ARCH/DESIGN/SECURITY sections the step cites.
Terse reference docs; ids are stable, cite them in commits and code comments where useful.

## Status

- Final. E0-E7 and gates G0-G7 met 2026-10-10; the audit after G7 and the steps after it (D30-D32, V8, V12, V13, X11, X13, PERF R7-R9) done; merged into `main` 2026-10-11. Final verification: PHASES "Wrap-up". Results: RESULTS.md. Open work and what is not planned: docs/ROADMAP.md.
- Baseline hashes (held through the final verification):
  - `pm_live --replay ~/data/pm-live/20261009T160027Z` -> `decision_hash 6facf575ad8d84bc`
  - `pm_stats ~/data/ref/pm-snap` (frozen copy, 44 files) -> `~/data/ref/pm_stats.tsv`, sha256 `dafd558b45dc3a1a...`

## Files

| File | Holds | Ids |
|---|---|---|
| DECISIONS.md | fixed choices, owner answers, open questions | D*, Q* |
| PHASES.md | phases, steps, checks, gates, status | E0.1 .. E7.n, G* |
| ARCH.md | modules, threads, data flow, record format, file map | A* |
| DESIGN.md | types, state machine, risk checks, ledger, sim model, arb algorithm | T*, S*, R*, L*, V*, X* |
| SECURITY.md | trust boundaries, rules, grep gates | SEC* |
| VENUE.md | exchange facts with sources, measured RTT and feed facts | F* |
| REFS.md | references mapped to components | RF* |
| HW.md | machine facts, measurement conditions | H*, W* |
| PERF.md | build and runtime measurements, what was adopted and why not | P*, R* |
| RESULTS.md | E6 latency, demos, makers and arbitrage at four latencies | - |

## Conventions

- Units (D5): Px int32 1e-4 USD; Qty int64 1e-6 share; Usd int64 1e-10 USD (Px*Qty exact). No double outside the maker model.
- Hot path rule: no alloc, lock, syscall, file I/O on the trading thread in steady state.
- Every step: inspect -> failing test -> code -> test -> measure if perf-relevant -> commit.
- Commit message: normal prose subject+body, max 3 lines, no attribution lines.
- Build: `cmake --preset <preset> && cmake --build build/<preset>` (VCPKG_ROOT set).
- Presets to keep green: release, native, asan, ubsan, tsan, clang (+ fuzz targets).
