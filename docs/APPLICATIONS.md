# Where these pieces are used

Each part of this repository does on real data what a production system does in a bank, broker,
exchange or regulator. No real orders are sent; the outputs are reports and measurements.

| Industry need | Who runs it | Here | Try it |
|---|---|---|---|
| Market surveillance: spotting spoofing, layering and quote stuffing | exchanges, broker compliance desks, regulators (the SEC's MIDAS system works from the same order-level data) | `itch_surveil`: ranked candidates with the evidence behind each | `hft surveil 2025-12-10` |
| Best-execution reporting | brokers, under SEC Rules 605 and 606; institutional transaction-cost analysis | `itch_bestex`: effective, realized and impact spreads by size, price improvement | `hft bestex 2025-12-10` |
| Market-data infrastructure: feed handling, book building, storage, point-in-time queries | data vendors, exchanges, every trading firm's data team | ITCH parser, order-by-order book (about 20 ns per update), compressed store, replay at 160M+ messages/s, checkpoints, `itch_book` | `hft replay 2025-12-10`, `hft book 2025-12-10 SPY 11:00:00` |
| Market-quality analytics | exchanges, regulators, academic market-structure research | `itch_study`: order flow imbalance, micro-price, queue value, order lifetimes, Hawkes, VPIN, trade costs | `hft study 2025-12-08,2025-12-09 2025-12-10` |
| Strategy research with honest evaluation | quant funds, market makers | queue-position backtester, walk-forward tests with bootstrap intervals, lead-lag, deflated Sharpe ratios on every sweep | `scripts/maker_walkforward.sh` |
| Research workbench: running studies, ranking results, inspecting books | quant research and trading desks (internal backtest dashboards and notebooks) | `hft studio`: job runner, risk-adjusted leaderboard, curve comparison, book scrubbing, kill switch | `hft studio` |
| Low-latency systems | trading firms; also ad-tech, gaming, telecom, databases | lock-free rings, zero-allocation hot path, isolated cores, PMU and Intel PT diagnosis, kernel-bypass analysis | `docs/exec/HW.md`, `docs/exec/PERF.md` |
| Pre-trade risk and order management | brokers and trading firms | risk checks, order manager, ledger, kill switch, fault-injected simulated venue | `hft demo` |

What it is not: a live trading system. Nasdaq's public feed is anonymous apart from attributed
orders, so surveillance output is a list of leads for a reviewer, not findings against anyone; a
production desk would join it with account-level order and trade records.
