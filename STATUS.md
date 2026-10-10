# Status

Complete (2026-10-11). Every phase in [docs/ROADMAP.md](docs/ROADMAP.md) is done; what is
deliberately left out is listed there under "Not planned". The research-phase log is in
[STATUS-research.md](https://github.com/Yash-Awasthi/hft/blob/research-archive/archive/docs/STATUS-research.md) at the research-archive tag.

## What runs

`pm_record` (into `~/data/pm`) and `pm_live --record` (a new `~/data/pm-live/<UTC>` directory every
6 h) run from pinned binaries in `~/data/bin` (D23); nothing starts at logon. Replace a pinned
binary with `install`, never `cp`, so the running process keeps its file. Paper runs of `pm_exec`
go to `~/data/exec/runs`.

    scripts/pm_supervise.sh ~/data/pm --cap-gb 50 --per-tag 8
    scripts/pm_live_supervise.sh ~/data/pm-live --record-cap-gb 20 --port 8088 --seconds 21600

## Final verification (2026-10-11, merged `main`)

- Tests: release, native and Clang 291 each (alloc_test included), asan, ubsan and tsan 287;
  one test skips without hardware performance counters.
- `scripts/check_parity.sh`: SAME on all seven recordings. `scripts/fault_campaign.sh` and
  `scripts/arb_fault_campaign.sh`: zero illegal reports, zero residual sets. `scripts/demo/*.sh`:
  expected hashes. `scripts/check_cli.sh`, `scripts/security_gates.sh`: clean.
- Fuzzers itch, tape, oms and ws, 60 s each: no crash.
- Baselines: `pm_live --replay ~/data/pm-live/20261009T160027Z` gives decision_hash
  6facf575ad8d84bc; `pm_stats ~/data/ref/pm-snap | sha256sum` starts dafd558b45dc3a1a.

Details: docs/exec/PHASES.md "Wrap-up"; measured results: docs/exec/RESULTS.md.
