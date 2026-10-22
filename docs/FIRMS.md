# How trading firms work, and where LobIQ fits

What electronic market makers and high-frequency firms do, from what they publish themselves, and
which part of LobIQ does the same job at small scale. LobIQ trades on paper only; the comparisons
are about the engineering and the research, not about scale or results.

## The business

Electronic market makers (Citadel Securities, Jane Street, Optiver, IMC, Susquehanna, HRT and
others) quote prices on both sides, on many venues, with their own capital: Optiver, Jane Street,
HRT, Jump and DRW trade only their own money, with no clients ([Pragmatic Engineer][pe]). A maker
earns the spread and exchange rebates, and loses to informed traders (adverse selection) and to
price moves while it holds inventory. Not all of it is microseconds: part of Jane Street's recent
gains came from strategies that hold positions for minutes to days ([Reuters, via search][reuters]).

| What decides a maker's PnL | Where LobIQ measures or models it |
|---|---|
| Spread captured, rebates and fees | Every backtest splits PnL into spread, inventory and fees, checked after every event (`src/backtest/`) |
| Adverse selection | The backtest's adverse-selection term; the Glosten-Milgrom model in the studio's Models tab; VPIN in `itch_study` |
| Inventory risk | Avellaneda-Stoikov and GLFT quoting (`avellaneda_stoikov`), the micro-price maker's inventory skew (`microprice`), risk limits |
| Queue position | Virtual orders join the real queues behind real orders (`src/engine/replay_exchange.hpp`); queue value in `itch_study` |
| Being picked off by a faster trader | Market-data and order-entry latency in every backtest; the latency sweep (`scripts/latency_sweep.sh`) |

## The stack

| Stage | What the firms publish | LobIQ |
|---|---|---|
| Market data in hardware | Optiver on FPGAs for low-latency trading ([FPL 2020 talk][optiver-fpga]); Jane Street runs chip-design puzzles and competitions ([blog][js]) | `fpga/itch_parser.sv`: ITCH 5.0 parsed 8 bytes a clock, one message a clock, checked field by field against the C++ decoder in Verilator |
| Memory and the TLB | HRT: with 4 KiB pages the TLB covers about 8 MiB; 2 MiB huge pages cover about 4 GiB and cut a random-access benchmark's runtime about 4.5 times ([HRT Beat][hrt]) | Pools that grow by moving pages and keep huge pages (`src/core/pool.hpp`); isolated cores, governor and THP settings (`docs/BARE_METAL.md`) |
| Moving messages between components | Jane Street on scaling a critical message bus ([blog][js]) | Lock-free single-producer single-consumer rings between feed, trading and recording threads (`src/core/spsc.hpp`) |
| Order books | Every firm rebuilds venues' books from their feeds | `src/book/tick_book.hpp`: 22-41 ns per update, 847M messages a day replayed and hash-checked |
| Features | Jane Street's event-driven feature engineering for neural networks ("Flex Model", [blog][js]) | Streaming features per event (`src/strategy/features.hpp`); multi-level OFI and the Markov chain of the book (`itch_mlofi`, `itch_markov`) |
| Simulation and synthetic data | Jane Street asks whether autoregressive diffusion can generate market data ([blog][js]); academic work does the same (TRADES, LOB-Bench) | A queue-reactive generator (`gen_day`) and a realism scorecard with LOB-Bench's statistics (`itch_realism`); LOBSTER export for the deep-learning benchmarks (`itch_lobster`) |
| Learning to trade | Reinforcement learning environments in research (mbt_gym, ABIDES-Gym, JAX-LOB) | A Gymnasium-style environment over the real queues (`rl/lobiq_env.py`) |
| Risk and order management | Pre-trade checks, kill switches, position limits | `src/exec/`: pre-trade risk, an order manager state machine, a ledger, a kill switch, fault injection |
| Languages | Jane Street: OCaml from research to trading and monitoring ([search summary][jsocaml]); HRT hires for low-level C++ | C++23 for the engines, Python for the studio and research glue, SystemVerilog for the parser |

## What LobIQ does not do

No live order entry (a CI gate forbids the code), no colocation, no kernel-bypass network cards and
no exchange connectivity. The latency figures are a laptop's, measured with care; a production
stack would add hardware timestamping, custom NICs and much more. The research results are honest
about decay: most edges found on December 2025 days had faded by June 2026.

## Sources

- [hrt]: Hudson River Trading, "Low Latency Optimization: Understanding Huge Pages (Part 1)", https://www.hudsonrivertrading.com/hrtbeat/low-latency-optimization-part-1/
- [js]: Jane Street blog, https://blog.janestreet.com/ (posts of August to October 2026: message bus, Flex Model, autoregressive diffusion for market data, ASIC puzzles)
- [optiver-fpga]: Williston Hayes (Optiver), "FPGAs and low latency trading", FPL 2020, https://www.youtube.com/watch?v=RCb8PsdipHI
- [pe]: The Pragmatic Engineer on Optiver, https://newsletter.pragmaticengineer.com/p/optiver
- [reuters]: Reuters on Jane Street's medium-frequency strategies, as summarised by a web search of October 2026
- [jsocaml]: Jane Street's technology page, as quoted in job listings found by the same search

[hrt]: https://www.hudsonrivertrading.com/hrtbeat/low-latency-optimization-part-1/
[js]: https://blog.janestreet.com/
[optiver-fpga]: https://www.youtube.com/watch?v=RCb8PsdipHI
[pe]: https://newsletter.pragmaticengineer.com/p/optiver
[reuters]: https://www.reuters.com/
[jsocaml]: https://www.janestreet.com/technology/
