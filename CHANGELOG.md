# Changelog

## Unreleased

- **FPGA ITCH parser.** `fpga/itch_parser.sv` parses a BinaryFILE stream at 8 bytes per clock
  and emits one decoded message per clock, one clock after its last byte. A Verilator testbench
  checks it field by field against the C++ decoder; CI runs it.
- **Host tuning measured.** Huge pages cut book p99 about 15%; `isolcpus` under WSL2 had no
  effect; kernel bypass does not apply here (`docs/exec/HW.md` W13-W16).

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
