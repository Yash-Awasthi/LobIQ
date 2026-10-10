# Status

Open work for [docs/ROADMAP.md](docs/ROADMAP.md). Finished work is in the git history and in
README "Performance"; the research-phase log is in
[STATUS-research.md](https://github.com/Yash-Awasthi/hft/blob/research-archive/archive/docs/STATUS-research.md) at the research-archive tag.

## Running

`pm_record` (into `~/data/pm`) and `pm_live --record` (a new `~/data/pm-live/<UTC>` directory every
6 h) run from pinned binaries in `~/data/bin` (D23); nothing starts at logon. Replace a pinned
binary with `install`, never `cp`. Paper runs of `pm_exec` go to `~/data/exec/runs`.

    scripts/pm_supervise.sh ~/data/pm --cap-gb 50 --per-tag 8
    scripts/pm_live_supervise.sh ~/data/pm-live --record-cap-gb 20 --port 8088 --seconds 21600

## Open

1. Execution layer (docs/exec): done through E7 and the post-G7 audit. Optional: faults V8, V12,
   V13; batch skew for groups over 15 legs (X11).
2. Arbitrage on the recordings (RESULTS.md E6.3): most windows are stale or below fees; attempts
   mostly lose a leg on near-resolved pairs. More recording days give a larger sample.
3. After about two weeks of recorder data: `pm_stats` and `pm_mm` over the full recording.

## Needs the owner

- A bare-Linux run for hardware counters and futex wake latency (WSL2: about 135 µs).
- Real order entry: an account, signing keys and the decision to trade.
- Hosting for a results page, if one is wanted.
