"""End to end: an episode of LobiqEnv on a synthetic day. An agent that always joins both best
quotes with 100 shares is the `naive` strategy without its inventory cap, so the episode must end
with naive's total to the cent; and the rewards must add up to it.
Usage: python3 rl/test_env.py <directory of built tools>"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lobiq_env import LobiqEnv  # noqa: E402

bin_dir = os.path.abspath(sys.argv[1])
venue = ["maker_rebate=2000", "taker_fee=3000"]
with tempfile.TemporaryDirectory() as tmp:
    store = os.path.join(tmp, "store")
    gen = subprocess.Popen([os.path.join(bin_dir, "gen_day"), "7", "60"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    subprocess.run([os.path.join(bin_dir, "ingest"), store], stdin=gen.stdout, check=True, capture_output=True)
    gen.wait()

    env = LobiqEnv(store, "QRSIM", step_ms=5000, params=venue, bin_dir=bin_dir)
    obs, _ = env.reset()
    assert {"t", "mid", "bid", "ask", "inventory", "pnl", "features"} <= set(obs), obs
    total, steps, info = obs["pnl"], 0, {}
    while True:
        obs, reward, done, truncated, info = env.step((0, 0, 100))
        assert not truncated
        total, steps = total + reward, steps + 1
        if done:
            break
    env.close()

    naive = subprocess.run([os.path.join(bin_dir, "itch_backtest"), store, "QRSIM", "naive", "max_inventory=1000000000", *venue],
                           capture_output=True, text=True, check=True).stdout
    want = next(line.split("\t")[1] for line in naive.splitlines() if line.startswith("total_usd\t"))
    assert steps > 100, steps
    assert int(info["fills"]) > 0, info
    assert info["total_usd"] == want, (info["total_usd"], want)
    assert abs(total - float(want)) < 1e-6, (total, want)
print(f"rl env ok: {steps} steps, {info['fills']} fills, total {want} USD, the same as naive")
