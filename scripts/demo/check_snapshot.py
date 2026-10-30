#!/usr/bin/env python3
"""The published studio snapshot must carry every tool's demo run: prints what it has, exits 1 on a gap."""
import json
import sys

demo = json.load(open(sys.argv[1], encoding="utf-8")).get("demo", {})
analysis, books, markov = demo.get("analysis", {}), demo.get("book", {}), demo.get("markov", {})
print(f"snapshot demo: {len(demo.get('days', []))} days, {len(books)} books, {len(markov)} Markov chains")
for kind, run in sorted(analysis.items()):
    print(f"  {kind:8} rc {run['rc']}  {len(run['text']):6} chars  {run['command']}")
want = {"surveil", "bestex", "replay", "mlofi", "realism", "leadlag", "study", "vol", "hawkes", "regime", "alpha", "options", "surface"}
gaps = sorted(want - {k for k, r in analysis.items() if r["rc"] == 0 and r["text"].strip()})
if gaps or len(books) < 120 or not markov:
    print(f"gaps: tools {gaps}, books {len(books)}/120, Markov chains {len(markov)}")
    sys.exit(1)
