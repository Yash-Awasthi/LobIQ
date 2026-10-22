"""Tests for hft studio: the metrics, and the server driving scripts/hft.

Without arguments the server runs against stub tools. With a directory of built tools
(python3 studio/test_studio.py build/release/apps) it also runs end to end on a synthetic Nasdaq day
and checks the backtest totals the demos pin.
"""

import json
import math
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REAL_BIN = os.path.abspath(sys.argv.pop(1)) if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]) else None
TMP = tempfile.mkdtemp(prefix="studio-test-").replace(os.sep, "/")
DATA = TMP + "/data"
os.makedirs(DATA + "/live/20261012T172311")
STUB = TMP + "/bin"
os.makedirs(STUB)

STUBS = {
    # Writes a report the way itch_backtest does; the PnL path rises 10 a minute with a dip.
    "itch_backtest": r"""#!/usr/bin/env bash
[ $# -lt 3 ] && { printf 'strategies:\n  naive                joins the touch\n  dp:<arg>             DP policy\nvenue keys: tick\n' >&2; exit 2; }
for ((i=1; i<=$#; i++)); do [ "${!i}" = --report ] && { j=$((i+1)); dir=${!j}; }; done
mkdir -p "$dir"
printf 'key,value\ntotal_usd,40.00\nspread_usd,55.00\ninventory_usd,-5.00\nfees_usd,-10.00\nadverse_usd,-3.00\nfills,4\norders,9\ncancels,5\nrejects,0\nvolume_shares,400\nend_inventory,0\nmax_abs_inventory,100\nfills_during_cancel,0\ndiverted_shares,0\nthrough_fills,0\nhidden_fills,0\nevents,1000\n' > "$dir/summary.csv"
printf 'minute,total_usd\n0,10\n1,20\n2,5\n3,30\n4,40\n' > "$dir/pnl_minutes.csv"
printf 'ts_ns,signed_shares,price,maker\n1,100,100000,1\n2,-100,100100,1\n3,100,100050,0\n4,-100,100150,1\n' > "$dir/fills.csv"
echo "<title>Backtest: $3 on $2</title>" > "$dir/report.html"
echo "total_usd	40.00"
echo "report: $dir/report.html" >&2
""",
    "itch_book": r"""#!/usr/bin/env bash
echo "$2 at $3: 7 orders resting; last message 09:59:59.000000001; replayed 12 messages from the open"
echo "  side     price       shares"
echo "  ask   100.0200         300"
echo "  ask   100.0300         500"
echo "  bid   100.0100         200"
echo "  bid   100.0000         900"
""",
    "itch_surveil": "#!/usr/bin/env bash\nprintf '# Surveillance report\\n\\n## Summary\\n\\n| | Candidates |\\n|---|---|\\n| Spoofing | 0 |\\n'\n",
}


def write_stubs():
    for name, body in STUBS.items():
        p = os.path.join(STUB, name)
        with open(p, "w", newline="\n") as f:
            f.write(body)
        os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
    store = DATA + "/store-S010125-v50"
    os.makedirs(store)
    for s in ("QRSIM", "QRALT"):
        open(f"{store}/{s}.idx", "w").close()


if REAL_BIN:
    os.environ["HFT_BIN"] = REAL_BIN
    store = DATA + "/store-S010125-v50"
    gen = subprocess.Popen([REAL_BIN + "/gen_day", "7", "390"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    subprocess.run([REAL_BIN + "/ingest", store], stdin=gen.stdout, check=True, capture_output=True)
    gen.wait()
    subprocess.run([REAL_BIN + "/checkpoint", store], check=True, capture_output=True)
else:
    write_stubs()
    os.environ["HFT_BIN"] = STUB
os.environ["HFT_DATA"] = DATA
os.environ.pop("HFT_REPORTS", None)

sys.path.insert(0, HERE)
import metrics  # noqa: E402
import server  # noqa: E402

server.TREES["out"] = TMP + "/out"
server.OUT = TMP + "/out"
SRV = server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
threading.Thread(target=SRV.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{SRV.server_address[1]}"


def call(path, body=None, headers=None, raw=False):
    h = {"Content-Type": "application/json"} if body is not None else {}
    h.update(headers or {})
    req = urllib.request.Request(BASE + path, data=None if body is None else json.dumps(body).encode(), headers=h)
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
            return r.status, data if raw else json.loads(data)
    except urllib.error.HTTPError as e:
        data = e.read()
        return e.code, data if raw else json.loads(data or b"{}")


def run_job(spec, timeout=300):
    code, job = call("/api/jobs", spec)
    assert code == 201, job
    end = time.time() + timeout
    while time.time() < end:
        _, v = call(f"/api/jobs/{job['id']}")
        if not v["running"]:
            return v
        time.sleep(0.1)
    raise AssertionError("job did not finish")


class Metrics(unittest.TestCase):
    def test_drawdown(self):
        self.assertEqual(metrics.drawdown([10, 20, 5, 30, 40]), (15, 1))
        self.assertEqual(metrics.drawdown([-5, -2]), (5, 2))

    def test_moments_of_symmetric_series(self):
        m, sd, skew, kurt = metrics.moments([-1, 1, -1, 1])
        self.assertEqual((m, skew, kurt), (0, 0, 1))
        self.assertAlmostEqual(sd, math.sqrt(4 / 3))

    def test_psr(self):
        self.assertAlmostEqual(metrics.psr(0, 100, 0, 3), 0.5)
        self.assertGreater(metrics.psr(0.3, 100, 0, 3), 0.99)
        self.assertLess(metrics.psr(0.3, 100, 0, 3, benchmark=0.5), 0.05)

    def test_more_trials_raise_the_hurdle(self):
        self.assertEqual(metrics.expected_max_sharpe([0.1]), 0)
        few = metrics.expected_max_sharpe([0.0, 0.1, 0.2])
        many = metrics.expected_max_sharpe([0.0, 0.1, 0.2] * 30)
        self.assertGreater(many, few)
        self.assertGreater(few, 0)

    def test_deflation_penalises_a_sweep(self):
        def run(drift):  # alternating +-1 minutes plus a drift, as a cumulative curve
            cum, x = [], 0.0
            for i in range(300):
                x += (1 if i % 2 else -1) + drift
                cum.append(x)
            return metrics.series_metrics(cum)
        alone = metrics.deflate([run(0.2)])[0]
        self.assertAlmostEqual(alone["dsr"], alone["psr"])
        sweep = metrics.deflate([run(d) for d in (0.0, 0.05, 0.1, 0.15, 0.2)])
        best = max(sweep, key=lambda r: r["sharpe_minute"])
        self.assertTrue(all(r["trials"] == 5 for r in sweep))
        self.assertGreater(best["psr"], 0.99)
        self.assertLess(best["dsr"], best["psr"])

    def test_fills(self):
        f = metrics.fill_metrics([(1, 100, 100000, True), (2, -100, 100100, True), (3, 100, 100050, False), (4, -100, 100150, True)])
        self.assertAlmostEqual(f["buy_vwap"], 10.0025)
        self.assertAlmostEqual(f["captured_spread"], 0.01)
        self.assertEqual(f["maker_share"], 0.75)


class Security(unittest.TestCase):
    def test_foreign_host_refused(self):
        self.assertEqual(call("/api/state", headers={"Host": "evil.example"})[0], 403)

    def test_cross_origin_post_refused(self):
        code, _ = call("/api/jobs", {"kind": "replay", "day": "S010125"}, headers={"Origin": "http://evil.example"})
        self.assertEqual(code, 403)

    def test_form_post_refused(self):
        code, _ = call("/api/jobs", {"kind": "demo"}, headers={"Content-Type": "text/plain"})
        self.assertEqual(code, 415)

    def test_arguments_validated(self):
        for spec in ({"kind": "backtest", "day": "S010125", "symbols": "QRSIM;id", "strategy": "naive"},
                     {"kind": "backtest", "day": "S010125", "symbols": "QRSIM", "strategy": "naive", "params": ["--report=/etc"]},
                     {"kind": "replay", "day": "../../etc"}, {"kind": "rm"}):
            self.assertEqual(call("/api/jobs", spec)[0], 400, spec)

    def test_no_path_traversal(self):
        self.assertEqual(call("/files/reports/../../../etc/passwd", raw=True)[0], 404)
        self.assertEqual(call("/files/nope/x", raw=True)[0], 404)


class Server(unittest.TestCase):
    def test_ui_served(self):
        code, body = call("/", raw=True)
        self.assertEqual(code, 200)
        self.assertIn(b"hft studio", body)
        self.assertEqual(call("/static/app.js", raw=True)[0], 200)

    def test_state_lists_days_and_runs(self):
        _, s = call("/api/state")
        self.assertEqual(s["days"][0]["day"], "S010125")
        self.assertEqual(s["days"][0]["date"], "2025-01-01")
        self.assertEqual(s["runs"][0]["run"], "20261012T172311")

    def test_strategies(self):
        names = [s["name"] for s in call("/api/strategies")[1]]
        self.assertIn("naive", names)

    def test_backtest_job_to_leaderboard(self):
        v = run_job({"kind": "backtest", "day": "2025-01-01", "symbols": "QRSIM", "strategy": "naive",
                     "params": ["maker_rebate=2000", "taker_fee=3000", "market_data_ns=50000", "order_entry_ns=20000"]})
        self.assertEqual(v["rc"], 0, v["lines"])
        self.assertTrue(v["report"].startswith("/files/reports/backtest-"), v)
        _, html = call(v["report"] + "/report.html", raw=True)
        self.assertIn(b"Backtest: naive", html)
        _, rows = call("/api/leaderboard")
        row = next(r for r in rows if r["path"] == v["report"])
        self.assertEqual(row["strategy"], "naive")
        self.assertIn("dsr", row)
        _, d = call("/api/report?path=" + v["report"])
        self.assertTrue(d["curve"])
        if REAL_BIN:
            self.assertEqual(row["summary"]["total_usd"], "5610.80")
        else:
            self.assertEqual(d["max_drawdown_usd"], 15)
            self.assertEqual(d["maker_share"], 0.75)

    def test_book(self):
        code, b = call("/api/book?day=S010125&symbol=QRSIM&time=10:00:00&levels=3")
        self.assertEqual(code, 200, b)
        self.assertTrue(b["header"].startswith("QRSIM at 10:00:00"))
        self.assertTrue(b["asks"] and b["bids"])
        self.assertLess(b["bids"][0]["price"], b["asks"][0]["price"])
        self.assertEqual(call("/api/book?day=S010125&symbol=QRSIM&time=noon")[0], 400)

    def test_stream(self):
        code, job = call("/api/jobs", {"kind": "surveil", "day": "S010125"})
        self.assertEqual(code, 201)
        _, body = call(f"/api/jobs/{job['id']}/stream", raw=True)
        self.assertIn(b"event: line", body)
        self.assertIn(b"event: done", body)

    def test_kill_switch_file(self):
        self.assertEqual(call("/api/kill", {"run": "20261012T172311"})[0], 200)
        self.assertTrue(os.path.exists(DATA + "/live/20261012T172311/KILL"))
        self.assertEqual(call("/api/kill", {"run": "../x"})[0], 400)


@unittest.skipUnless(REAL_BIN, "needs built tools")
class EndToEnd(unittest.TestCase):
    def test_sweep_is_deflated(self):
        v = run_job({"kind": "backtest", "day": "S010125", "symbols": "QRSIM", "strategy": "imbalance",
                     "params": ["maker_rebate=2000", "taker_fee=3000", "market_data_ns=50000", "order_entry_ns=20000"],
                     "sweeps": ["threshold=0.2,0.4,0.6,0.8,1", "size=100,200"]})
        self.assertEqual(v["rc"], 0, v["lines"])
        _, rows = call("/api/leaderboard")
        sweep = [r for r in rows if r["group"] == v["report"]]
        self.assertEqual(len(sweep), 10)
        self.assertEqual(max(float(r["summary"]["total_usd"]) for r in sweep), 8551.50)
        self.assertTrue(all(r["trials"] == 10 for r in sweep))
        self.assertTrue(all(r["dsr"] is None or r["dsr"] <= r["psr"] + 1e-12 for r in sweep))

    def test_replay_job(self):
        v = run_job({"kind": "replay", "day": "S010125"})
        self.assertEqual(v["rc"], 0, v["lines"])
        self.assertIn("0 check failures", v["lines"][-1])


if __name__ == "__main__":
    try:
        unittest.main(verbosity=2)
    finally:
        SRV.shutdown()
        shutil.rmtree(TMP, ignore_errors=True)
