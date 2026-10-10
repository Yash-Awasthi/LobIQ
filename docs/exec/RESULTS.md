# Results

Measured, not tuned (D12). Machine and conditions: HW.md (turbo off, WSL2, vCPU pinning only, recorders running).
Build: native preset. Every row names its command; run from the repository root.

## E6.1 Latency and throughput

`taskset -c 2 build/native/apps/pm_exec --replay ~/data/pm-live/20261009T160027Z --config config/exec.cfg --set arb=1 --profile`
(three runs; the middle one shown, the others within 5% at p50 and p99):

| stage | p50 ns | p99 ns | p99.9 ns | count | what it covers |
|---|---|---|---|---|---|
| parse | 527 | 3419 | 9303 | 1,982,218 | one recorded message, JSON to events |
| engine | 556 | 2788 | 14406 | 1,982,218 | one message through books, makers, executor, risk, order manager and venue |
| submit | 172 | 649 | 1054 | 22,555 | intent to venue request: risk checks and order table (intent-to-request) |
| deliver | 77 | 346 | 784 | 44,361 | one venue report through the order manager, ledger and its strategy |
| venue | 209 | 1334 | 3727 | 20,751 | matching and scheduling in each venue run that did work |

Throughput: 514k messages/s (whole replay, one thread, decompression included).
In-process timings include a tsc start/stop pair (about 10 ns) and cold caches between messages; the
isolated micro-benchmarks give the warm cost:

`taskset -c 2 build/native/bench/hft_bench --benchmark_filter="Risk|Oms" --benchmark_repetitions=3`

| case | mean |
|---|---|
| pre-trade risk check (2, 40, 200 tokens) | 19.5-19.7 ns |
| order cycle: submit with risk, ack, fill, cancel, cancel ack | 59.8 ns (12.0 ns per operation) |

The venue path costs the engine about 3x the no-order path (`build/native/apps/pm_live --replay ...`: engine p50 189 ns):
the makers' orders, cancels and reports are real work there. One hot spot was removed on the way (PERF R6).

## E6.2 Demos

Each is one command and checks its decision hash (exit 1 if it differs).

| script | shows | decision_hash_v2 |
|---|---|---|
| `scripts/demo/a_clean.sh` | makers and the executor on the reference recording, resolutions and tick changes applied | 9599a30f66a8e803 |
| `scripts/demo/b_faults.sh` | dropped acks, duplicates, failed settlements, disconnects: 2059 orders went Unknown and were reconciled; 0 illegal reports, 0 mismatches, no kill | 7573009ce7421dae |
| `scripts/demo/c_leg_risk.sh` | 30% dropped acks with the executor: all 17 attempts lost a leg and were completed; 0 residual sets | 8f91ab8ac960f49a |
| `scripts/demo/d_kill.sh` | a paper run tripped by its KILL file 20 s in: every order cancelled (0 open at the end), 156 later orders refused, kill.log written | 76e7e15544ecbb02 |

`d_kill` replays `~/data/exec/runs/demo-kill`, a 60 s paper run recorded with `pm_exec --paper`.

## E6.3 Makers and arbitrage at four latencies

`scripts/latency_sweep.sh [recording]`: config/exec.cfg with `arb=1`, one-way latency half the round trip
(measured 182 ms, D4, and 50, 150, 300 ms). PnL in USD after fees. Markets that resolve during a recording pay out (L4), so event sets bought
before a resolution end as cash; ledger_pnl_usd values what is still held at the mid marks of the last risk tick.

Reference recording `~/data/pm-live/20261009T160027Z` (2.0M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 105 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -12.148430 | 0.00 | 8.093590 | -36.838552 | none | 9599a30f66a8e803 |
| 50 | 81 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -11.133820 | 0.00 | 8.136020 | -30.910676 | none | 0188cde73892595c |
| 150 | 101 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -12.148430 | 0.00 | 8.093590 | -35.134622 | none | f7c520bc9a21a635 |
| 300 | 125 | 33 | 305 | 18 | 0 | 1 | 16 | 0 | bound:0,depth:9,retries:1 | 0 | 16 | -11.301370 | 0.00 | 6.992050 | -30.844729 | none | 6715ed0e2fea924b |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 50 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 150 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 300 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:11,size:0,edge:58,leg_risk:0.

Recording `~/data/pm-live/20261009T135806Z` (0.7M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 70 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -63.393429 | none | caacb1ff97a6379f |
| 50 | 60 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.057470 | 0.00 | 0.034880 | -63.000244 | none | 4bfe688a0f98c40d |
| 150 | 71 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -63.043429 | none | d69b3a596f22477a |
| 300 | 84 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -70.896770 | none | 3ab044cea0439c7c |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:8,size:2,edge:46,leg_risk:0; 50 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:7,size:2,edge:47,leg_risk:0; 150 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:7,size:2,edge:47,leg_risk:0; 300 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:8,size:2,edge:46,leg_risk:0.

Reading: most windows on these recordings sit on books that have not changed for 2 s (stale) or do
not clear fees and allowances (edge). Of the attempts taken, nearly every one lost a leg and was then
completed at a loss; the attempts traced (scratch trace, 25 ms one way) were on near-resolved pairs, one
leg at 0.007-0.04 and the other at 0.95-0.99, where the cheap leg's ask had moved by arrival. The
arbitrage outcomes change little across the four latencies on these recordings. No threshold was
changed to improve these numbers (D12).

Leg-risk check (X13, on by default): a window is taken only if its net edge covers the loss of
reversing the other legs at the visible book should one leg fill nothing. On these recordings it
refused no window outright and moved realised PnL at 182 ms from -11.76 to -12.15 USD (a window it
refused at first was taken later on a worse book). The losses here come from how often a leg misses
(nearly every attempt: the cheap leg's quote has moved by arrival), not from the size of a miss,
which X13 bounds. A miss probability would need a model estimated on some days and tested on later
ones; not done (D12).

Limits of the model, stated with every result (DESIGN V): level-2 data has no order ids, so queue position
is estimated; other traders do not react to our orders; latency is modelled, not measured per order; fills
of our resting orders are decided by trade price against our quote (D21).
