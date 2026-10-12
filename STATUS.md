# Status

v1.1 (2026-10-11), now on bare-metal Arch Linux. Nothing is open ([docs/ROADMAP.md](docs/ROADMAP.md));
what is deliberately left out is listed there under "Not planned". Changes per version: [CHANGELOG.md](CHANGELOG.md).
The research-phase log is in
[STATUS-research.md](https://github.com/Yash-Awasthi/hft/blob/research-archive/archive/docs/STATUS-research.md) at the research-archive tag.

## What runs

Nothing runs on its own: no service, scheduled task or logon hook. The recorders were stopped on
2026-10-11; their recordings stay in `~/data`. To record again, start them by hand (stop with
Ctrl-C or SIGTERM):

    scripts/pm_supervise.sh ~/data/pm --cap-gb 50 --per-tag 8
    scripts/pm_live_supervise.sh ~/data/pm-live --record-cap-gb 20 --port 8088 --seconds 21600

On GitHub, CI builds and tests every pull request; on main it also publishes the container image
(`ghcr.io/yash-awasthi/hft`) and the demo reports (https://yash-awasthi.github.io/hft/); a `v*`
tag publishes a release with Linux binaries.

## Verification (2026-10-12, bare metal, main after the restore)

Same checks as below, all passing: native and release 306 tests each, `check_parity.sh` SAME on
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
