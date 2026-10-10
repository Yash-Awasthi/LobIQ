# Sample data

`pm-20261009T1600-20min/` is the first 20 minutes of a `pm_live --record` session from
2026-10-09 16:00 UTC: 177,910 market events from Polymarket's public market WebSocket for the most
traded events of that day, in the recorder's own format (`session.tsv` lists the tokens and event
groups; `c0/*.jsonl.zst` holds one `<receive ns> <message>` line per record, in processing order).

It is here so the paper-trading pipeline can be replayed without recording first:

```
pm_exec --replay data/sample/pm-20261009T1600-20min --config config/exec.cfg --set arb=1 --report out/paper
scripts/demo/paper.sh
```

The data is public market data published by Polymarket and is included only as a small sample
for demonstrating this software; it is not investment information. Polymarket's terms of use
apply to it.

Nasdaq data is not included. `gen_day` writes a synthetic session from a queue-reactive model
(`scripts/demo/backtest.sh`), and `scripts/ingest_day.sh` downloads a real day from Nasdaq's
public sample files.
