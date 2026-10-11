# Results

Measured, not tuned (D12). Machine and conditions: HW.md W1-W16 (turbo off, WSL2, vCPU pinning only, recorders running).
Build: native preset. Every row names its command; run from the repository root.

## E6.1 Latency and throughput

`taskset -c 2 build/native/apps/pm_exec --replay ~/data/pm-live/20261009T160027Z --config config/exec.cfg --set arb=1 --profile`
(three runs on 2026-10-11, final code; the middle one shown, the others within 5% at p50 and p99):

| stage | p50 ns | p99 ns | p99.9 ns | count | what it covers |
|---|---|---|---|---|---|
| parse | 593 | 3681 | 9737 | 1,982,218 | one recorded message, JSON to events |
| engine | 555 | 3019 | 14840 | 1,982,218 | one message through books, makers, executor, risk, order manager and venue |
| submit | 177 | 712 | 1198 | 22,585 | intent to venue request: risk checks and order table (intent-to-request) |
| deliver | 78 | 371 | 960 | 44,418 | one venue report through the order manager, ledger and its strategy |
| venue | 205 | 1395 | 3384 | 20,797 | matching and scheduling in each venue run that did work |

Throughput: 489k messages/s (whole replay, one thread, decompression included; 508k and 492k in the
other runs). In-process timings include a tsc start/stop pair (about 10 ns) and cold caches between
messages; the isolated micro-benchmarks give the warm cost:

`taskset -c 2 build/native/bench/hft_bench --benchmark_filter="Risk|Oms" --benchmark_repetitions=3`

| case | mean |
|---|---|
| pre-trade risk check (2, 40, 200 tokens) | 25.1-25.6 ns |
| order cycle: submit with risk, ack, fill, cancel, cancel ack | 77.2-79.9 ns (15.9 ns per operation) |

These were 19.4 ns and 57-58 ns at the E6.1 commit (85f70d8, back to back, same clock): the risk code
is unchanged, but link-time optimisation no longer inlines `Risk::evaluate` and `Ledger::fill` into the
benchmark loop (PERF R10). `pm_exec` calls them out of line in both builds: submit p50 167 vs 169 ns.

The venue path costs the engine about 3x the no-order path (`build/native/apps/pm_live --replay ...`: engine p50 190 ns):
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

All complete recordings at the measured round trip (182 ms). Skipped: `20261009T135737Z` (a 29 s start-up
run, 1,671 messages, no windows).

| recording | messages | windows | attempts | complete | empty | completed_after_leg_loss | unwound | residual_groups | realised_usd | fees_usd | ledger_pnl_usd | kill |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `20261009T135806Z` | 706,354 | 111 | 2 | 0 | 0 | 2 | 0 | 0 | -0.081160 | 0.035980 | -63.393429 | none |
| `20261009T160027Z` | 1,982,402 | 305 | 18 | 0 | 0 | 18 | 0 | 0 | -12.148430 | 8.093590 | -36.838552 | none |
| `20261010T084623Z` | 123,707 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 0.000000 | 0.000000 | 0.000000 | none |
| `20261010T111523Z` | 36,183 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0.000000 | 0.000000 | 0.000000 | none |
| `20261010T112758Z` | 2,541,915 | 381 | 29 | 1 | 4 | 24 | 0 | 0 | -51.622270 | 18.017470 | -115.266560 | none |
| `20261010T172810Z` | 1,178,505 | 238 | 12 | 1 | 1 | 8 | 1 | 0 | -3.599340 | 2.663040 | 9.969122 | none |

Recording `~/data/pm-live/20261009T135806Z` (0.7M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 70 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -63.393429 | none | caacb1ff97a6379f |
| 50 | 60 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.057470 | 0.00 | 0.034880 | -63.000244 | none | 4bfe688a0f98c40d |
| 150 | 71 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -63.043429 | none | d69b3a596f22477a |
| 300 | 84 | 4 | 111 | 2 | 0 | 0 | 2 | 0 | bound:0,depth:1,retries:0 | 0 | 2 | -0.081160 | 0.00 | 0.035980 | -70.896770 | none | 3ab044cea0439c7c |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:8,size:2,edge:46,leg_risk:0; 50 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:7,size:2,edge:47,leg_risk:0; 150 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:7,size:2,edge:47,leg_risk:0; 300 ms: young:0,stale:50,frozen:0,one_sided:3,self_cross:8,size:2,edge:46,leg_risk:0.

Reference recording `~/data/pm-live/20261009T160027Z` (2.0M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 105 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -12.148430 | 0.00 | 8.093590 | -36.838552 | none | 9599a30f66a8e803 |
| 50 | 81 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -11.133820 | 0.00 | 8.136020 | -30.910676 | none | 0188cde73892595c |
| 150 | 101 | 36 | 305 | 18 | 0 | 0 | 18 | 0 | bound:0,depth:9,retries:0 | 0 | 18 | -12.148430 | 0.00 | 8.093590 | -35.134622 | none | f7c520bc9a21a635 |
| 300 | 125 | 33 | 305 | 18 | 0 | 1 | 16 | 0 | bound:0,depth:9,retries:1 | 0 | 16 | -11.301370 | 0.00 | 6.992050 | -30.844729 | none | 6715ed0e2fea924b |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 50 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 150 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:8,size:0,edge:61,leg_risk:0; 300 ms: young:0,stale:189,frozen:0,one_sided:14,self_cross:11,size:0,edge:58,leg_risk:0.

Recording `~/data/pm-live/20261010T112758Z` (2.5M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 89 | 54 | 381 | 29 | 1 | 4 | 24 | 0 | bound:0,depth:8,retries:0 | 0 | 23 | -51.622270 | 0.00 | 18.017470 | -115.266560 | none | 0474ac6343021dcb |
| 50 | 76 | 58 | 381 | 30 | 1 | 3 | 25 | 1 | bound:0,depth:8,retries:1 | 0 | 25 | -53.741330 | 0.00 | 18.193290 | -96.255945 | none | 4b2d2a365982f4a0 |
| 150 | 87 | 54 | 381 | 29 | 1 | 4 | 24 | 0 | bound:0,depth:8,retries:0 | 0 | 23 | -51.484630 | 0.00 | 18.006290 | -117.494757 | none | 9aae599cd95e8464 |
| 300 | 107 | 56 | 381 | 29 | 1 | 4 | 24 | 0 | bound:0,depth:7,retries:0 | 0 | 23 | -50.557870 | 0.00 | 17.994030 | -90.721283 | none | ed00ccd54cdbfc31 |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:72,frozen:0,one_sided:7,self_cross:33,size:10,edge:204,leg_risk:9; 50 ms: young:0,stale:72,frozen:0,one_sided:7,self_cross:21,size:10,edge:214,leg_risk:9; 150 ms: young:0,stale:72,frozen:0,one_sided:7,self_cross:30,size:10,edge:207,leg_risk:9; 300 ms: young:0,stale:72,frozen:0,one_sided:7,self_cross:39,size:10,edge:198,leg_risk:9.

Recording `~/data/pm-live/20261010T172810Z` (1.2M messages):

| rtt_ms | maker_fills | taker_fills | windows | attempts | complete | empty | completed_after_leg_loss | unwound | frozen | residual_groups | merges | realised_usd | sets_held | fees_usd | ledger_pnl_usd | kill | decision_hash_v2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 182 | 30 | 24 | 238 | 12 | 1 | 1 | 8 | 1 | bound:0,depth:7,retries:1 | 0 | 10 | -3.599340 | 0.00 | 2.663040 | 9.969122 | none | c71b2ba91cb34c08 |
| 50 | 28 | 23 | 238 | 12 | 1 | 1 | 9 | 0 | bound:0,depth:7,retries:1 | 0 | 9 | -5.884190 | 0.00 | 2.840290 | -0.461728 | none | 5297cbc6cd7ac8d9 |
| 150 | 30 | 24 | 238 | 12 | 1 | 1 | 8 | 1 | bound:0,depth:7,retries:1 | 0 | 9 | -3.599340 | 0.00 | 2.663040 | 9.319122 | none | daec1f9c4cb9f478 |
| 300 | 32 | 22 | 238 | 12 | 1 | 1 | 9 | 0 | bound:0,depth:7,retries:1 | 0 | 9 | -1.428260 | 0.00 | 2.978900 | 14.140202 | none | b8553977ab5e3fdb |

Windows never attempted, by the filter that last refused them: 182 ms: young:0,stale:19,frozen:0,one_sided:4,self_cross:9,size:3,edge:161,leg_risk:20; 50 ms: young:0,stale:19,frozen:0,one_sided:4,self_cross:7,size:3,edge:163,leg_risk:20; 150 ms: young:0,stale:19,frozen:0,one_sided:4,self_cross:8,size:3,edge:162,leg_risk:20; 300 ms: young:0,stale:19,frozen:0,one_sided:4,self_cross:11,size:3,edge:159,leg_risk:20.

`20261010T084623Z`: 6 windows, no attempts, no fills at any latency (filters at 182 ms: young:0,stale:0,frozen:0,one_sided:0,self_cross:0,size:0,edge:6,leg_risk:0); decision_hash_v2 at 182/50/150/300 ms: d0e9c9ca90a08098, 4143a6d371302803, cb7d2bf0ad8616cf, 00ceed3c557c1dc8.

`20261010T111523Z`: 0 windows, no attempts, no fills at any latency (filters at 182 ms: young:0,stale:0,frozen:0,one_sided:0,self_cross:0,size:0,edge:0,leg_risk:0); decision_hash_v2 at 182/50/150/300 ms: bb701e5d9c06096a, feced489d5056ac1, b9ccb9cc54571590, b9e5cd72e466343f.

Reading: most windows on these recordings sit on books that have not changed for 2 s (stale) or do
not clear fees and allowances (edge). Of the 61 attempts at 182 ms, two completed as planned, five
filled nothing, 52 lost a leg and were then completed at a loss, one was unwound, and one has no
counted outcome but left no residual set; the attempts traced (scratch trace, 25 ms one way) were on near-resolved
pairs, one leg at 0.007-0.04 and the other at 0.95-0.99, where the cheap leg's ask had moved by
arrival. Realised arbitrage PnL over the six recordings at 182 ms: -67.45 USD, with 28.81 USD of
fees. The outcomes change little across the four latencies. No threshold was changed to improve
these numbers (D12).

Leg-risk check (X13, on by default): a window is taken only if its net edge covers the loss of
reversing the other legs at the visible book should one leg fill nothing. On the reference recording
it refused no window outright and moved realised PnL at 182 ms from -11.76 to -12.15 USD (a window it
refused at first was taken later on a worse book); it refused 9 windows on `20261010T112758Z` and 20 on `20261010T172810Z`. The losses here come from how often a leg misses
(nearly every attempt: the cheap leg's quote has moved by arrival), not from the size of a miss,
which X13 bounds. A miss probability would need a model estimated on some days and tested on later
ones; not planned (ROADMAP, D12).

Limits of the model, stated with every result (DESIGN V): level-2 data has no order ids, so queue position
is estimated; other traders do not react to our orders; latency is modelled, not measured per order; fills
of our resting orders are decided by trade price against our quote (D21).
