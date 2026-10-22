# hft studio

A local web UI over the `hft` command: one page that runs every tool, shows what they produced and
ranks the results. Python standard library and plain JavaScript, no build step and no packages.

```
hft studio                       # http://127.0.0.1:8000/  (--port N, or HFT_STUDIO_PORT)
hft studio --bind 0.0.0.0        # inside a container; publish the port with -p 8000:8000
```

It reads the same places as `hft`: stores and live runs under `HFT_DATA` (default `~/data`),
backtest reports under `HFT_REPORTS` (default `$HFT_DATA/reports`), demo output under `out/`.

## Pages

| Page | What it does | Runs |
|---|---|---|
| Overview | Stores on disk, live runs, recent jobs; download a day; run the demos | `hft get`, `hft demo` |
| Backtest | Day, symbols, strategy, venue parameters and sweep axes; output streams as it runs, then the run's metrics | `hft backtest` |
| Leaderboard | Every backtest report on disk with risk-adjusted metrics, sortable; a row opens the equity curve, drawdown and cost breakdown | reads `summary.csv`, `pnl_minutes.csv`, `fills.csv` |
| Order book | Depth ladder of any symbol at any moment, stepping one second at a time; mid, spread, L1 micro-price and depth imbalance | `hft book` |
| Analysis | Surveillance, execution quality, full-day replay, lead-lag and the microstructure study, Markdown output rendered as tables | `hft surveil`, `bestex`, `replay`, `leadlag`, `study` |
| Live paper | Start a paper run on the live feed, embed its dashboard, list past runs, review a run, trip a run's kill switch | `hft live`, `hft review` |
| Jobs | Every job with its command, exit code, time, log and report link; stop a running one | |

## Metrics

From each report's one-minute PnL path: Sharpe (annualised by sqrt(252 * 390)), the probabilistic
Sharpe ratio against 0, maximum drawdown and the longest time under water, profit factor, hit rate,
skewness and kurtosis; from its fills: maker share and captured spread per share.

The runs of one sweep share a group, and each run's **deflated Sharpe ratio** is its PSR against
the best Sharpe expected from luck across that many trials. A sweep of ten settings will usually
have a winner with a high PSR; the DSR says whether it beats what ten tries at random would give.
Formulas and sources: [METHODS.md](METHODS.md#evaluation). Code: `studio/metrics.py`.

## Safety

The studio starts processes, so it guards what can reach it:

- It answers only requests whose Host is a loopback name (`127.0.0.1`, `localhost`, `[::1]`), so a
  DNS-rebinding page cannot use it.
- Every state-changing request must be a JSON POST, and a request carrying a foreign `Origin` is
  refused, so another site cannot drive it from a visitor's browser.
- Job arguments are matched against fixed patterns (dates, symbols, strategy names, numeric
  `key=value`) and passed to `scripts/hft` as an argument list, never through a shell; no value
  can start with `-` and become an option.
- Files are served only from the stores' report, live and demo directories, with paths resolved
  and checked to stay inside them.
- The kill switch writes the run's `KILL` file, which `pm_exec` polls; nothing is sent anywhere.

## Tests

`studio/test_studio.py`, run by ctest:

- `Studio.StubTools`: the metrics on series with known answers; the server against stub tools
  (jobs, streaming, leaderboard, book parsing, kill switch) and its refusals (foreign Host,
  cross-origin and non-JSON posts, malformed arguments, path traversal).
- `Studio.EndToEnd`: the same with the built tools on a synthetic day (`gen_day 7 390`): a backtest
  through the web API gives the total the demos pin (5,610.80 USD), a 10-run sweep gives the pinned
  best (8,551.50 USD) and is deflated as one set of 10 trials, and a full replay reports 0 check
  failures.

```
python3 studio/test_studio.py                      # stub tools
python3 studio/test_studio.py build/release/apps   # end to end
```
