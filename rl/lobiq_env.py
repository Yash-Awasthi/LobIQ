"""A Gymnasium-style environment over LobIQ's queue-position backtester (the `agent` strategy,
strategies/agent.cpp): an agent quotes in the real Nasdaq queues of a stored day, with the
backtester's latency, fees, risk limits and accounting. Standard library only; the API follows
Gymnasium's reset() / step(), like mbt_gym and ABIDES-Gym.

    env = LobiqEnv("~/data/store-S121025-v50", "INTC", step_ms=1000,
                   params=["maker_rebate=2000", "taker_fee=3000", "market_data_ns=50000", "order_entry_ns=20000"])
    obs, info = env.reset()
    while True:
        obs, reward, terminated, truncated, info = env.step((0, 0, 100))  # join both sides, 100 shares
        if terminated:
            break

Action: (bid_offset, ask_offset, size), offsets in ticks behind the best (0 joins it, -1 leaves that
side empty), or None for no quotes. Observation: a dict with the strategy's clock, mid, best prices
and sizes, inventory, PnL and the backtester's streaming features. Reward: the change in total PnL
(USD) over the step. The episode ends with the day; the last step's info holds the run's summary.
"""

import json
import os
import shutil
import subprocess


def find_bin():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for d in (os.environ.get("HFT_BIN"), os.path.join(here, "bin"), os.path.join(here, "build/native/apps"),
              os.path.join(here, "build/release/apps")):
        if d and os.path.isfile(os.path.join(d, "itch_backtest")):
            return d
    found = shutil.which("itch_backtest")
    return os.path.dirname(found) if found else None


class LobiqEnv:
    def __init__(self, store, symbol, step_ms=1000, params=(), bin_dir=None):
        self.store, self.symbol, self.step_ms, self.params = os.path.expanduser(store), symbol, step_ms, list(params)
        self.bin_dir = bin_dir or find_bin()
        if not self.bin_dir:
            raise FileNotFoundError("itch_backtest not found: build LobIQ or set HFT_BIN")
        self.proc, self.last_obs, self.last_pnl = None, None, 0.0

    def _read_obs(self):
        """Next observation, or None at the end of the day (then self.summary holds the run's totals)."""
        for line in self.proc.stdout:
            if line.startswith("obs "):
                return json.loads(line[4:])
            if "\t" in line:
                k, v = line.rstrip("\n").split("\t", 1)
                self.summary[k] = v
        return None

    def reset(self, seed=None, options=None):
        self.close()
        cmd = [os.path.join(self.bin_dir, "itch_backtest"), self.store, self.symbol, "agent", f"step_ms={self.step_ms}", *self.params]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                     text=True, bufsize=1)
        self.summary = {}
        self.last_obs = self._read_obs()
        if self.last_obs is None:
            raise RuntimeError("the backtest produced no observation: " + json.dumps(self.summary))
        self.last_pnl = self.last_obs["pnl"]
        return self.last_obs, {}

    def step(self, action):
        line = "flat" if action is None else "%d %d %d" % tuple(int(x) for x in action)
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass
        obs = self._read_obs()
        if obs is None:  # the day is over: the final total, after the end-of-day flatten, closes the episode
            self.proc.wait()
            total = float(self.summary.get("total_usd", self.last_pnl))
            reward, self.last_pnl = total - self.last_pnl, total
            return self.last_obs, reward, True, False, dict(self.summary)
        reward = obs["pnl"] - self.last_pnl
        self.last_obs, self.last_pnl = obs, obs["pnl"]
        return obs, reward, False, False, {}

    def close(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.proc = None


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 3:
        sys.exit("usage: python3 rl/lobiq_env.py <store> <SYMBOL> [key=value ...]   (runs a joining policy)")
    env = LobiqEnv(sys.argv[1], sys.argv[2], params=sys.argv[3:])
    obs, _ = env.reset()
    total, steps = 0.0, 0
    while True:
        side = -1 if obs["inventory"] > 300 else 0, -1 if obs["inventory"] < -300 else 0  # stop adding past 300 shares
        obs, reward, done, _, info = env.step((side[0], side[1], 100))
        total, steps = total + reward, steps + 1
        if done:
            break
    print(f"{steps} steps, total reward {total:.2f} USD, backtester total {info.get('total_usd')} USD")
