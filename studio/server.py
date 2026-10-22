"""hft studio: a local web UI over the hft command. Standard library only.

It runs scripts/hft as jobs (argument lists, never a shell), streams their output, and reads the
stores, live runs and reports on disk. Only loopback Host names are answered, and state-changing
requests must be same-origin JSON, so other web pages cannot drive it.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metrics  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STATIC = os.path.join(HERE, "static")
HFT = os.path.join(ROOT, "scripts", "hft")
DATA = os.environ.get("HFT_DATA") or os.path.join(os.path.expanduser("~"), "data")
REPORTS = os.environ.get("HFT_REPORTS") or os.path.join(DATA, "reports")
OUT = os.path.join(ROOT, "out")
TREES = {"reports": REPORTS, "out": OUT, "live": os.path.join(DATA, "live")}
LOOPBACK = {"127.0.0.1", "localhost", "[::1]"}

DAY = r"(?:S\d{6}|\d{4}-\d{2}-\d{2})"
SYM = r"[A-Z0-9.]{1,8}"
PATTERNS = {
    "day": re.compile(rf"^{DAY}$"),
    "days": re.compile(rf"^{DAY}(?:,{DAY})*$"),
    "symbols": re.compile(rf"^{SYM}(?:,{SYM})*$"),
    "symbol": re.compile(rf"^{SYM}$"),
    "strategy": re.compile(r"^[a-z_][a-z0-9_]*(?::[A-Za-z0-9_.]+)?$"),
    "param": re.compile(r"^[a-z_][a-z0-9_]*=[0-9eE.+\-]+$"),
    "sweep": re.compile(r"^[a-z_][a-z0-9_]*=[0-9eE.+\-]+(?:,[0-9eE.+\-]+)*$"),
    "pairs": re.compile(rf"^{SYM}:{SYM}(?::-1)?(?:,{SYM}:{SYM}(?::-1)?)*$"),
    "run": re.compile(r"^\d{8}T\d{6}$"),
    "time": re.compile(r"^\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?$"),
    "minutes": re.compile(r"^[1-9]\d{0,3}$"),
    "levels": re.compile(r"^[1-9]\d?$"),
    "recording": re.compile(r"^(?:sample|\d{8}T\d{6})$"),
    "quoter": re.compile(r"^[a-z_][a-z0-9_]*$"),
}
SAMPLE = os.path.join(ROOT, "data", "sample", "pm-20261009T1600-20min")


class BadRequest(Exception):
    pass


def field(spec, name, kind, required=True):
    v = spec.get(name)
    if v in (None, "", []):
        if required:
            raise BadRequest(f"{name} is required")
        return None
    if not isinstance(v, str) or not PATTERNS[kind].match(v):
        raise BadRequest(f"bad {name}: {v!r}")
    return v


def strings(spec, name, kind):
    vs = spec.get(name) or []
    if not isinstance(vs, list) or len(vs) > 32:
        raise BadRequest(f"bad {name}")
    for v in vs:
        if not isinstance(v, str) or not PATTERNS[kind].match(v):
            raise BadRequest(f"bad {name}: {v!r}")
    return vs


def hft_args(spec):
    """Validated job spec -> arguments to scripts/hft. Every value matches a fixed pattern."""
    kind = spec.get("kind")
    opt = lambda *xs: [x for x in xs if x]
    if kind == "backtest":
        args = ["backtest", field(spec, "day", "day"), field(spec, "symbols", "symbols"), field(spec, "strategy", "strategy")]
        args += strings(spec, "params", "param")
        for s in strings(spec, "sweeps", "sweep"):
            args += ["--sweep", s]
        return args
    if kind == "replay":
        return ["replay", field(spec, "day", "day")]
    if kind in ("surveil", "bestex"):
        return opt(kind, field(spec, "day", "day"), field(spec, "symbols", "symbols", False))
    if kind == "study":
        return ["study", field(spec, "train", "days"), field(spec, "test", "days")]
    if kind == "leadlag":
        return opt("leadlag", field(spec, "days", "days"), field(spec, "pairs", "pairs", False))
    if kind == "book":
        return opt("book", field(spec, "day", "day"), field(spec, "symbol", "symbol"), field(spec, "time", "time"),
                   field(spec, "levels", "levels", False))
    if kind == "get":
        return ["get", *field(spec, "days", "days").split(",")]
    if kind in ("live", "paper"):
        args = ["live", field(spec, "minutes", "minutes")] if kind == "live" else ["paper", field(spec, "recording", "recording")]
        q = field(spec, "quoter", "quoter", False)
        if q:
            args += ["--quoter", q]
        if spec.get("arb") is True:
            args += ["--set", "arb=1"]
        for kv in strings(spec, "params", "param"):
            args += ["--set", kv]
        return args
    if kind == "review":
        return ["review", field(spec, "run", "run")]
    if kind == "demo":
        return ["demo", OUT]
    raise BadRequest(f"unknown kind {kind!r}")


def find_bash():
    # On Windows the first bash on PATH is often the WSL launcher; Git's bash runs the stub tools.
    git = shutil.which("git") if os.name == "nt" else None
    gb = git and os.path.join(os.path.dirname(os.path.dirname(git)), "bin", "bash.exe")
    return gb if gb and os.path.isfile(gb) else shutil.which("bash") or "bash"


BASH = find_bash()


def run_hft(args, timeout=None):
    return subprocess.run([BASH, HFT, *args], cwd=ROOT, capture_output=True, text=True,
                          timeout=timeout)


class Job:
    def __init__(self, jid, spec, args):
        self.id, self.spec, self.args = jid, spec, args
        self.lines, self.rc, self.report = [], None, None
        self.started, self.ended = time.time(), None
        self.cond = threading.Condition()
        self.proc = subprocess.Popen([BASH, HFT, *args], cwd=ROOT, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, text=True, bufsize=1)
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.proc.stdout:
            line = line.rstrip("\n")
            m = re.match(r"^report: (.+?)(?:/(?:report|index)\.html)?$", line)
            with self.cond:
                if m:
                    self.report = m.group(1)
                self.lines.append(line)
                self.cond.notify_all()
        rc = self.proc.wait()
        with self.cond:
            self.rc, self.ended = rc, time.time()
            self.cond.notify_all()

    def view(self, tail=None):
        with self.cond:
            lines = self.lines if tail is None else self.lines[-tail:]
            return {"id": self.id, "spec": self.spec, "args": self.args, "rc": self.rc, "running": self.rc is None,
                    "started": self.started, "ended": self.ended, "report": rel(self.report), "lines": list(lines),
                    "line_count": len(self.lines)}


JOBS, JOBS_LOCK, NEXT_ID = {}, threading.Lock(), [1]
MAX_JOBS = 100


def start_job(spec):
    args = hft_args(spec)
    with JOBS_LOCK:
        jid = NEXT_ID[0]
        NEXT_ID[0] += 1
        JOBS[jid] = Job(jid, spec, args)
        for old in sorted(JOBS)[:-MAX_JOBS]:
            if JOBS[old].rc is not None:
                del JOBS[old]
        return JOBS[jid]


def rel(path):
    """Absolute path inside one of the served trees -> /files/<tree>/<path>, else None."""
    if not path:
        return None
    p = os.path.realpath(path)
    for name, base in TREES.items():
        b = os.path.realpath(base)
        if p == b or p.startswith(b + os.sep):
            return f"/files/{name}/" + os.path.relpath(p, b).replace(os.sep, "/").removeprefix(".")
    return None


def resolve(tree, sub):
    base = TREES.get(tree)
    if base is None:
        return None
    b = os.path.realpath(base)
    p = os.path.realpath(os.path.join(b, urllib.parse.unquote(sub)))
    return p if p == b or p.startswith(b + os.sep) else None


def du(path):
    total = 0
    for dp, _, fs in os.walk(path):
        for f in fs:
            try:
                total += os.path.getsize(os.path.join(dp, f))
            except OSError:
                pass
    return total


def list_days():
    out = []
    if not os.path.isdir(DATA):
        return out
    for name in sorted(os.listdir(DATA)):
        m = re.match(r"^store-S(\d{2})(\d{2})(\d{2})-v50$", name)
        d = os.path.join(DATA, name)
        if m and os.path.isdir(d):
            syms = sum(1 for f in os.listdir(d) if f.endswith(".idx"))
            out.append({"day": f"S{m[1]}{m[2]}{m[3]}", "date": f"20{m[3]}-{m[1]}-{m[2]}", "symbols": syms, "bytes": du(d)})
    return out


def read_tsv(path):
    try:
        with open(path) as f:
            return dict(line.rstrip("\n").split("\t", 1) for line in f if "\t" in line)
    except OSError:
        return {}


def list_runs():
    base = TREES["live"]
    out = []
    if not os.path.isdir(base):
        return out
    for name in sorted(os.listdir(base), reverse=True):
        d = os.path.join(base, name)
        if not os.path.isdir(d):
            continue
        s = read_tsv(os.path.join(d, "summary.tsv"))
        status = "killed" if os.path.exists(os.path.join(d, "KILL")) else ("done" if s else "running or stopped")
        rep = os.path.join(d, "report", "report.html")
        out.append({"run": name, "status": status, "summary": s, "bytes": du(d),
                    "report": rel(rep) if os.path.isfile(rep) else None})
    return out


def find_reports(limit=500):
    """Backtest report directories (summary.csv) under the served trees, newest first."""
    found = []
    for tree in ("reports", "out"):
        base = TREES[tree]
        if not os.path.isdir(base):
            continue
        for dp, dns, fs in os.walk(base):
            if dp[len(base):].count(os.sep) >= 4:
                dns[:] = []
            if "summary.csv" in fs:
                found.append(dp)
    found.sort(key=lambda d: os.path.getmtime(os.path.join(d, "summary.csv")), reverse=True)
    return found[:limit]


def report_entry(d, bootstrap=False):
    m = metrics.report_metrics(d, bootstrap)
    if m is None:
        return None
    parent = os.path.dirname(d)
    group = parent if os.path.isfile(os.path.join(parent, "index.html")) else d
    m.update(path=rel(d), group=rel(group), label=os.path.basename(d), mtime=os.path.getmtime(os.path.join(d, "summary.csv")))
    return m


def leaderboard():
    """Every report with metrics; one symbol's runs in one sweep are deflated together, as itch_backtest does."""
    groups = {}
    for d in find_reports():
        e = report_entry(d)
        if e:
            groups.setdefault((e["group"], e["symbol"]), []).append(e)
    rows = []
    for runs in groups.values():
        rows += metrics.deflate(runs)
    return rows


_TOOL_LISTS = {}


def tool_list(cmd):
    """Names and help printed by `hft strategies` / `hft quoters`, cached; empty without built tools."""
    if cmd not in _TOOL_LISTS:
        out = []
        if bin_dir():
            r = run_hft([cmd], timeout=30)
            for line in r.stdout.splitlines():
                m = re.match(r"^\s*(\S+)\s+(.*)$", line)
                if m:
                    out.append({"name": m[1].replace(":<arg>", ":"), "help": m[2]})
        if not out:
            return []  # not cached: tools built later are picked up
        _TOOL_LISTS[cmd] = out
    return _TOOL_LISTS[cmd]


def catalog():
    """Strategies and quoters with their keys. The built tools decide what exists; catalog.json adds
    keys and defaults, and stands in (marked unavailable) before anything is built."""
    with open(os.path.join(HERE, "catalog.json"), encoding="utf-8") as f:
        cat = json.load(f)
    out = {"venues": cat["venues"], "maker_keys": cat["maker_keys"], "recordings": recordings()}
    for kind, cmd in (("strategies", "strategies"), ("quoters", "quoters")):
        known = {e["name"]: e for e in cat[kind]}
        live = tool_list(cmd)
        if live:
            out[kind] = [dict(known.get(e["name"], {"keys": {}}), name=e["name"], help=e["help"], available=True) for e in live]
        else:
            out[kind] = [dict(e, available=False) for e in cat[kind]]
    return out


def recordings():
    """Live sessions a paper replay can run on: the bundled 20 minutes, then recorded live runs."""
    out = [{"id": "sample", "label": "Bundled: Polymarket, 2026-10-09 16:00, 20 minutes"}] if os.path.isdir(SAMPLE) else []
    out += [{"id": r["run"], "label": f"Live run {r['run']} ({r['status']})"} for r in list_runs()]
    return out


def paper_reports():
    """Paper replay reports (hft paper), newest first, with their summary."""
    out = []
    if not os.path.isdir(REPORTS):
        return out
    for name in sorted(os.listdir(REPORTS), reverse=True):
        d = os.path.join(REPORTS, name)
        if name.startswith("paper-") and os.path.isfile(os.path.join(d, "summary.csv")):
            out.append({"name": name, "summary": metrics.read_kv(os.path.join(d, "summary.csv")),
                        "report": rel(os.path.join(d, "report.html"))})
    return out


def parse_book(text):
    lines = text.splitlines()
    asks, bids = [], []
    for line in lines:
        m = re.match(r"^\s+(bid|ask)\s+([0-9.]+)\s+(\d+)$", line)
        if m:
            (bids if m[1] == "bid" else asks).append({"price": float(m[2]), "shares": int(m[3])})
    return {"header": lines[0] if lines else "", "asks": asks, "bids": bids}


def bin_dir():
    if os.environ.get("HFT_BIN"):
        return os.environ["HFT_BIN"]
    for c in ("bin", "build/native/apps", "build/release/apps"):
        if os.path.isfile(os.path.join(ROOT, c, "itch_backtest")):
            return os.path.join(ROOT, c)
    return None


def state():
    return {"data": DATA, "reports": REPORTS, "bin": bin_dir(), "days": list_days(), "runs": list_runs(),
            "jobs": [j.view(tail=3) for j in sorted(JOBS.values(), key=lambda j: -j.id)]}


MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".csv": "text/csv; charset=utf-8",
        ".tsv": "text/tab-separated-values; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml",
        ".png": "image/png", ".txt": "text/plain; charset=utf-8", ".log": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8"}


class Handler(BaseHTTPRequestHandler):
    server_version = "hft-studio"

    def log_message(self, fmt, *a):
        if os.environ.get("HFT_STUDIO_LOG"):
            super().log_message(fmt, *a)

    def _host_ok(self):
        host = (self.headers.get("Host") or "").lower()
        name = host.rsplit(":", 1)[0] if not host.startswith("[") else host.split("]")[0] + "]"
        return name in LOOPBACK

    def _send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, allow_nan=False, default=str).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, json.loads(json.dumps(obj, default=str), parse_constant=lambda c: None))

    def _file(self, path):
        if not path or not os.path.isfile(path):
            return self._send(404, {"error": "not found"})
        with open(path, "rb") as f:
            self._send(200, f.read(), MIME.get(os.path.splitext(path)[1].lower(), "application/octet-stream"))

    def do_GET(self):
        if not self._host_ok():
            return self._send(403, {"error": "loopback only"})
        u = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        p = u.path
        try:
            if p in ("/", "/index.html"):
                return self._file(os.path.join(STATIC, "index.html"))
            if p.startswith("/static/") and "/" not in p[8:]:
                return self._file(os.path.join(STATIC, p[8:]))
            if p == "/api/state":
                return self._json(200, state())
            if p == "/api/catalog":
                return self._json(200, catalog())
            if p == "/api/paper":
                return self._json(200, paper_reports())
            if p == "/api/leaderboard":
                return self._json(200, leaderboard())
            if p == "/api/report":
                tree, _, sub = q.get("path", "").removeprefix("/files/").partition("/")
                d = resolve(tree, sub)
                e = report_entry(d, bootstrap=True) if d and os.path.isdir(d) else None
                return self._json(200, e) if e else self._send(404, {"error": "no report there"})
            if p == "/api/book":
                spec = dict(q, kind="book")
                r = run_hft(hft_args(spec), timeout=120)
                if r.returncode != 0:
                    return self._json(400, {"error": (r.stderr or r.stdout).strip()[-500:]})
                return self._json(200, parse_book(r.stdout))
            m = re.match(r"^/api/jobs/(\d+)(/stream)?$", p)
            if m:
                job = JOBS.get(int(m[1]))
                if not job:
                    return self._send(404, {"error": "no such job"})
                return self._stream(job) if m[2] else self._json(200, job.view())
            if p.startswith("/files/"):
                tree, _, sub = p[7:].partition("/")
                path = resolve(tree, sub)
                if path and os.path.isdir(path):
                    path = os.path.join(path, "index.html") if os.path.isfile(os.path.join(path, "index.html")) else os.path.join(path, "report.html")
                return self._file(path)
            return self._send(404, {"error": "not found"})
        except BadRequest as e:
            return self._json(400, {"error": str(e)})
        except subprocess.TimeoutExpired:
            return self._json(504, {"error": "timed out"})

    def _stream(self, job):
        """Server-sent events: one 'line' event per output line, then 'done' with the exit code."""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        sent = 0
        try:
            while True:
                with job.cond:
                    while sent == len(job.lines) and job.rc is None:
                        job.cond.wait(15)
                        if sent == len(job.lines) and job.rc is None:
                            break
                    new, rc = job.lines[sent:], job.rc
                for line in new:
                    self.wfile.write(f"event: line\ndata: {json.dumps(line)}\n\n".encode())
                sent += len(new)
                if rc is not None and sent == len(job.lines):
                    self.wfile.write(f"event: done\ndata: {json.dumps(job.view(tail=0))}\n\n".encode())
                    self.wfile.flush()
                    return
                if not new:
                    self.wfile.write(b": keepalive\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        # Read the body before any refusal: closing with unread bytes resets the connection.
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = -1
        if not 0 <= n <= 65536:
            self.close_connection = True
            return self._send(413, {"error": "body too large or bad length"})
        raw = self.rfile.read(n)
        if not self._host_ok():
            return self._send(403, {"error": "loopback only"})
        origin = self.headers.get("Origin")
        if origin and urllib.parse.urlsplit(origin).netloc.lower() != (self.headers.get("Host") or "").lower():
            return self._send(403, {"error": "cross-origin request refused"})
        if (self.headers.get("Content-Type") or "").split(";")[0].strip() != "application/json":
            return self._send(415, {"error": "send application/json"})
        try:
            body = json.loads(raw or b"{}")
            if not isinstance(body, dict):
                raise BadRequest("send a JSON object")
            p = self.path
            if p == "/api/jobs":
                return self._json(201, start_job(body).view())
            m = re.match(r"^/api/jobs/(\d+)/cancel$", p)
            if m:
                job = JOBS.get(int(m[1]))
                if not job:
                    return self._send(404, {"error": "no such job"})
                if job.rc is None:
                    job.proc.terminate()
                return self._json(200, {"ok": True})
            if p == "/api/kill":
                run = field(body, "run", "run")
                d = os.path.join(TREES["live"], run)
                if not os.path.isdir(d):
                    raise BadRequest("no such run")
                open(os.path.join(d, "KILL"), "a").close()  # pm_exec polls for this file and trips the switch
                return self._json(200, {"ok": True})
            return self._send(404, {"error": "not found"})
        except (BadRequest, ValueError) as e:
            return self._json(400, {"error": str(e)})


def snapshot(out):
    """Read-only copy of the UI over the reports in OUT, at OUT/studio/: static files, no server."""
    out = os.path.abspath(out)
    for k in TREES:
        TREES[k] = out if k == "out" else os.path.join(out, ".none")
    dest = os.path.join(out, "studio")
    os.makedirs(os.path.join(dest, "static"), exist_ok=True)
    for f in os.listdir(STATIC):
        shutil.copy(os.path.join(STATIC, f), os.path.join(dest, "static", f))
    with open(os.path.join(STATIC, "index.html"), encoding="utf-8") as f:
        page = f.read().replace("<head>", '<head>\n<meta name="hft-snapshot" content="1">', 1)
    with open(os.path.join(dest, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    rows = []
    for d in find_reports():
        e = report_entry(d, bootstrap=True)
        if e:
            rows.append(e)
    groups = {}
    for r in rows:
        groups.setdefault((r["group"], r["symbol"]), []).append(r)
    for runs in groups.values():
        metrics.deflate(runs)
    # /files/out/X is ../X from OUT/studio/.
    data = {"state": {"data": "snapshot", "reports": "", "bin": "snapshot", "days": [], "runs": [], "jobs": []},
            "leaderboard": rows, "catalog": catalog(), "paper": []}
    text = json.dumps(json.loads(json.dumps(data, default=str), parse_constant=lambda c: None), allow_nan=False)
    with open(os.path.join(dest, "snapshot.json"), "w", encoding="utf-8") as f:
        f.write(text.replace('"/files/out/', '"../'))
    print(f"studio snapshot: {dest}/index.html ({len(rows)} reports)")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hft studio", description="Local web UI for the hft toolkit.")
    ap.add_argument("--port", type=int, default=int(os.environ.get("HFT_STUDIO_PORT", 8000)))
    ap.add_argument("--bind", default="127.0.0.1", help="address to listen on (0.0.0.0 inside a container)")
    ap.add_argument("--snapshot", metavar="OUT", help="write a read-only copy of the UI over the reports in OUT and exit")
    a = ap.parse_args(argv)
    if a.snapshot:
        return snapshot(a.snapshot)
    srv = ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    print(f"hft studio on http://127.0.0.1:{srv.server_address[1]}/  (data {DATA}, Ctrl-C stops)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        for j in JOBS.values():
            if j.rc is None:
                j.proc.terminate()


if __name__ == "__main__":
    main()
