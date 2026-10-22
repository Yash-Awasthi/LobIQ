"""Risk and performance metrics for backtest reports (DIR/summary.csv, pnl_minutes.csv, fills.csv).

Sharpe ratios are on one-minute PnL changes. The probabilistic and deflated Sharpe ratios follow
Bailey and Lopez de Prado (2012, 2014); docs/METHODS.md gives the formulas.
"""

import csv
import math
import os
import random
import re
from statistics import NormalDist

EULER_GAMMA = 0.5772156649015329
MINUTES_PER_YEAR = 252 * 390
_N = NormalDist()


def read_kv(path):
    with open(path, newline="") as f:
        return {r["key"]: r["value"] for r in csv.DictReader(f)}


def read_minutes(path):
    with open(path, newline="") as f:
        return [float(r["total_usd"]) for r in csv.DictReader(f)]


def read_fills(path):
    with open(path, newline="") as f:
        return [(int(r["ts_ns"]), int(r["signed_shares"]), int(r["price"]), r["maker"] == "1") for r in csv.DictReader(f)]


def moments(xs):
    """Mean, sample standard deviation, skewness and (non-excess) kurtosis."""
    n = len(xs)
    if n < 2:
        return (xs[0] if xs else 0.0), 0.0, 0.0, 3.0
    m = sum(xs) / n
    d2 = sum((x - m) ** 2 for x in xs)
    sd = math.sqrt(d2 / (n - 1))
    pop = d2 / n
    if pop == 0:
        return m, 0.0, 0.0, 3.0
    skew = sum((x - m) ** 3 for x in xs) / n / pop**1.5
    kurt = sum((x - m) ** 4 for x in xs) / n / pop**2
    return m, sd, skew, kurt


def drawdown(cum):
    """Largest fall from a running peak (the day starts at 0) and the longest run below a peak, in steps."""
    peak, worst, under, longest = 0.0, 0.0, 0, 0
    for x in cum:
        if x >= peak:
            peak, under = x, 0
        else:
            under += 1
            longest = max(longest, under)
        worst = max(worst, peak - x)
    return worst, longest


def psr(sr, n, skew, kurt, benchmark=0.0):
    """Probability that the true per-step Sharpe exceeds the benchmark, given n observations."""
    if n < 2:
        return float("nan")
    denom = 1 - skew * sr + (kurt - 1) / 4 * sr * sr
    if denom <= 0:
        return float("nan")
    return _N.cdf((sr - benchmark) * math.sqrt(n - 1) / math.sqrt(denom))


def expected_max_sharpe(trial_srs):
    """Expected best per-step Sharpe among len(trial_srs) unskilled trials with the trials' variance."""
    k = len(trial_srs)
    if k < 2:
        return 0.0
    _, sd, _, _ = moments(trial_srs)
    return sd * ((1 - EULER_GAMMA) * _N.inv_cdf(1 - 1 / k) + EULER_GAMMA * _N.inv_cdf(1 - 1 / (k * math.e)))


def block_bootstrap(steps, reps=2000, seed=1):
    """95% interval of the summed PnL and the share of resampled days that lose, from a circular
    block bootstrap with blocks of about T^(1/3) minutes (keeps short-range autocorrelation)."""
    n = len(steps)
    if n < 2:
        return float("nan"), float("nan"), float("nan")
    rng = random.Random(seed)
    b = max(1, round(n ** (1 / 3)))
    totals = []
    for _ in range(reps):
        t, k = 0.0, 0
        while k < n:
            s = rng.randrange(n)
            for j in range(min(b, n - k)):
                t += steps[(s + j) % n]
            k += b
        totals.append(t)
    totals.sort()
    return totals[int(0.025 * reps)], totals[int(0.975 * reps) - 1], sum(1 for t in totals if t < 0) / reps


def series_metrics(cum):
    steps = [cum[0]] + [b - a for a, b in zip(cum, cum[1:])] if cum else []
    m, sd, skew, kurt = moments(steps)
    sr = m / sd if sd > 0 else 0.0
    dd, under = drawdown(cum)
    gains = sum(x for x in steps if x > 0)
    losses = -sum(x for x in steps if x < 0)
    active = [x for x in steps if x != 0]
    return {
        "minutes": len(steps),
        "mean_minute_usd": m,
        "sd_minute_usd": sd,
        "sharpe_minute": sr,
        "sharpe_annual": sr * math.sqrt(MINUTES_PER_YEAR),
        "skew": skew,
        "kurtosis": kurt,
        "psr": psr(sr, len(steps), skew, kurt),
        "max_drawdown_usd": dd,
        "longest_underwater_min": under,
        "profit_factor": gains / losses if losses > 0 else (math.inf if gains > 0 else float("nan")),
        "hit_rate": sum(1 for x in active if x > 0) / len(active) if active else float("nan"),
    }


def fill_metrics(fills):
    buys = [(q, p) for _, q, p, _ in fills if q > 0]
    sells = [(-q, p) for _, q, p, _ in fills if q < 0]
    vwap = lambda xs: sum(q * p for q, p in xs) / sum(q for q, _ in xs) / 1e4 if xs else float("nan")
    shares = sum(abs(q) for _, q, _, _ in fills)
    out = {
        "fill_count": len(fills),
        "maker_share": sum(abs(q) for _, q, _, mk in fills if mk) / shares if shares else float("nan"),
        "buy_vwap": vwap(buys),
        "sell_vwap": vwap(sells),
    }
    out["captured_spread"] = out["sell_vwap"] - out["buy_vwap"]
    return out


_TITLE = re.compile(r"<title>Backtest: (.+?) on (.+?)</title>")


def report_metrics(d, bootstrap=False):
    """Everything known about one backtest report directory, or None if it is not one."""
    if not os.path.isfile(os.path.join(d, "summary.csv")):
        return None
    summary = read_kv(os.path.join(d, "summary.csv"))
    out = {"summary": summary, "strategy": None, "symbol": None}
    try:
        with open(os.path.join(d, "report.html"), encoding="utf-8", errors="replace") as f:
            t = _TITLE.search(f.read(4096))
        if t:
            out["strategy"], out["symbol"] = (s.replace("&amp;", "&").replace("&lt;", "<") for s in t.groups())
    except OSError:
        pass
    cum = []
    p = os.path.join(d, "pnl_minutes.csv")
    if os.path.isfile(p):
        cum = read_minutes(p)
    out["curve"] = cum
    out.update(series_metrics(cum))
    if bootstrap and cum:
        steps = [cum[0]] + [b - a for a, b in zip(cum, cum[1:])]
        out["pnl_ci_low"], out["pnl_ci_high"], out["p_loss"] = block_bootstrap(steps)
    p = os.path.join(d, "fills.csv")
    if os.path.isfile(p):
        out.update(fill_metrics(read_fills(p)))
    return out


def deflate(runs):
    """Add each run's deflated Sharpe: its PSR against the best Sharpe expected from luck over these trials."""
    srs = [r["sharpe_minute"] for r in runs]
    sr0 = expected_max_sharpe(srs)
    for r in runs:
        r["trials"] = len(runs)
        r["sharpe_hurdle_minute"] = sr0
        r["dsr"] = psr(r["sharpe_minute"], r["minutes"], r["skew"], r["kurtosis"], sr0)
    return runs
