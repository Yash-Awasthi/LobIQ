# lobiq studio

A local web UI over the `lobiq` command: one page that runs every tool, shows what they produced and
ranks the results. Python standard library and plain JavaScript, no build step and no packages.

```
lobiq studio                       # http://127.0.0.1:8000/  (--port N, or HFT_STUDIO_PORT)
lobiq studio --bind 0.0.0.0        # inside a container; publish the port with -p 8000:8000
```

Strategies and quote models come from the built tools; before a build, `studio/catalog.json` lists
the built-in ones with their keys and defaults (marked "not built"), and `Studio.EndToEnd` checks
that it names exactly what the tools register. A parameter left at its default is not passed, so the
tool's own default stays the one that counts. Without built tools the Overview shows how to get
them: GitHub Codespaces, WSL on Windows, the container image or a Linux build.

The header's theme button cycles auto (the system setting), light and dark.

It reads the same places as `lobiq`: stores and live runs under `HFT_DATA` (default `~/data`),
backtest reports under `HFT_REPORTS` (default `$HFT_DATA/reports`), demo output under `out/`.

## Pages

| Page | What it does | Runs |
|---|---|---|
| Overview | Stores on disk, live runs, recent jobs; download a day; run the demos | `lobiq get`, `lobiq demo` |
| Backtest | A past Nasdaq day: day, symbols, strategy with an input per parameter, a venue and latency preset, optional sweep axes and extra keys; output streams as it runs, then the run's metrics | `lobiq backtest` |
| Paper replay | A recorded live prediction-market session (the bundled 20 minutes or any live run): quote model, maker parameters, arbitrage on or off; the run's PnL, orders and fills, and every past replay | `lobiq paper` |
| Leaderboard | Every backtest report on disk with risk-adjusted metrics, sortable; a row opens the equity curve, drawdown and cost breakdown; selected runs can be compared or combined on a Markowitz frontier | reads `summary.csv`, `pnl_minutes.csv`, `fills.csv` |
| Order book | Depth ladder of any symbol at any moment, stepping one second at a time; mid, spread, L1 micro-price and depth imbalance | `lobiq book` |
| Models | Kyle's insider equilibrium, Glosten-Milgrom spreads and price learning, Almgren-Chriss optimal execution and its frontier, Avellaneda-Stoikov quotes; every input live, no tools needed (works on the published site too) | `studio/static/models.js` |
| Analysis (2023-2025 research) | Multi-level OFI and cross-impact, LOBSTER export, and a market realism scorecard of a real day against the simulator | `lobiq mlofi`, `lobiq lobster`, `lobiq realism` |
| Prediction | Markov chain of the top of book: P(next move up) per state as a heat map, the state graph, and the afternoon's out-of-sample hit rate against always guessing one direction | `lobiq markov` |
| Analysis | Surveillance, execution quality, full-day replay, lead-lag and the microstructure study, Markdown output rendered as tables | `lobiq surveil`, `bestex`, `replay`, `leadlag`, `study` |
| Live paper | Start a paper run on the live feed with a chosen quote model, embed its dashboard, list past runs, review a run, trip a run's kill switch | `lobiq live`, `lobiq review` |
| Jobs | Every job with its command, exit code, time, log and report link; stop a running one | |

## Metrics

From each report's one-minute PnL path: Sharpe (annualised by sqrt(252 * 390)), the probabilistic
Sharpe ratio against 0, maximum drawdown and the longest time under water, profit factor, hit rate,
skewness and kurtosis; from its fills: maker share and captured spread per share.

One symbol's runs in one sweep are one set of trials, and each run's **deflated Sharpe ratio** is
its PSR against the best Sharpe expected from luck across that many trials. `itch_backtest`
computes the same statistics in C++ and writes them to `summary.csv`; the studio recomputes them
from the PnL path, so it also scores reports written before the backend had them, and
`Studio.EndToEnd` checks that both agree. A sweep of ten settings will usually
have a winner with a high PSR; the DSR says whether it beats what ten tries at random would give.
Formulas and sources: [METHODS.md](METHODS.md#evaluation). Code: `studio/metrics.py`.

## Snapshot

`python3 studio/server.py --snapshot OUT` writes a read-only copy of the UI over the backtest
reports in OUT to `OUT/studio/`: the same page and metrics, from a `snapshot.json`, with no server.
`scripts/demo/all.sh` writes one after the demos, so the published demo reports include the
leaderboard (`studio/index.html` next to `index.html`).

## Themes

Both follow the system setting. Dark: black `#0a0a0a`, cerulean `#007ba7` for actions and bids,
neon red `#ff2d95` for losses and asks. Light: ice silver `#dde3e8`, aubergine `#3a0f34` for
actions and bids, marigold `#eaaa00` for losses and asks. Every text colour keeps at least 4.5:1
contrast; where an accent is too pale or dark for text on its background (marigold on silver,
`#007ba7` on black) it fills bars, borders and shading, and text uses a darker marigold `#8f5000`
or a lighter cerulean `#3fb0dc`. Tokens: the top of `studio/static/style.css`.

## Safety

The studio starts processes, so it guards what can reach it:

- It answers only requests whose Host is a loopback name (`127.0.0.1`, `localhost`, `[::1]`), so a
  DNS-rebinding page cannot use it.
- Every state-changing request must be a JSON POST, and a request carrying a foreign `Origin` is
  refused, so another site cannot drive it from a visitor's browser.
- Job arguments are matched against fixed patterns (dates, symbols, strategy names, numeric
  `key=value`) and passed to `scripts/lobiq` as an argument list, never through a shell; no value
  can start with `-` and become an option.
- Files are served only from the stores' report, live and demo directories, with paths resolved
  and checked to stay inside them.
- The kill switch writes the run's `KILL` file, which `pm_exec` polls; nothing is sent anywhere.

## Tests

`studio/test_studio.py`, run by ctest:

- `Studio.StubTools`: the metrics on series with known answers; the server against stub tools
  (jobs, streaming, leaderboard, book parsing, kill switch) and its refusals (foreign Host,
  cross-origin and non-JSON posts, malformed arguments, path traversal).
- `Snapshot`: the static copy keeps backtest reports only, with relative links, and carries the
  bootstrap interval for the detail view.
- `Studio.EndToEnd`: the same with the built tools on a synthetic day (`gen_day 7 390`): a backtest
  through the web API gives the total the demos pin (5,610.80 USD), a 10-run sweep gives the pinned
  best (8,551.50 USD) and is deflated as one set of 10 trials, and a full replay reports 0 check
  failures.

```
python3 studio/test_studio.py                      # stub tools
python3 studio/test_studio.py build/release/apps   # end to end
```
