# Methods

Every model and estimator behind the results pages, with the formula as implemented, its source,
and the code and test that pin it down. Notation: $b, a$ best bid and ask prices, $q^b, q^a$ their
sizes, $m = (b + a) / 2$ the mid, one tick = $0.01 unless stated. Times are ITCH nanoseconds since
midnight ET.

## Order book quantities

- **Mid, spread, depth.** $m = (b + a)/2$, spread $a - b$ in ticks, best-level depth $(q^b + q^a)/2$.
  A typical depth is the exponential average $D \leftarrow 0.999\,D + 0.001\,(q^b + q^a)/2$ per book
  event (`itch_study`, `surveil.hpp`).
- **Imbalance.** $I = q^b / (q^b + q^a) \in [0, 1]$.
- **Size-weighted mid.** $I a + (1 - I) b$: leans toward the side with less size.

## Order flow imbalance (Cont, Kukanov and Stoikov 2014)

Per book event $n$, the order flow at the best quotes:

$$e_n = \mathbb 1[b_n \ge b_{n-1}]\,q^b_n - \mathbb 1[b_n \le b_{n-1}]\,q^b_{n-1}
       - \mathbb 1[a_n \le a_{n-1}]\,q^a_n + \mathbb 1[a_n \ge a_{n-1}]\,q^a_{n-1}.$$

Over 10 s intervals $k$: $\mathrm{OFI}_k = \sum_{n \in k} e_n$ and $\Delta m_k = \beta\,\mathrm{OFI}_k + \varepsilon_k$,
fitted by OLS per symbol on the train days. Out-of-sample $R^2 = 1 - \mathrm{SSE}/\mathrm{SST}$ with
the train $\beta$ (negative when worse than the test mean). The universal model replaces $\beta$ by
$c / D^\lambda$, fitted as $\log \beta_w = \log c - \lambda \log D_w$ over half-hour windows $w$.

Code: `ofi_event`, `ols`, `r2_of` in `src/strategy/microstructure.hpp`. Tests: `Ofi.FollowsThePaperCaseByCase`,
`Ols.RecoversALineAndScoresOutOfSample`.

## Micro-price (Stoikov 2018)

States $x = (\text{spread} \in \{1, 2\}\text{ ticks}, \lfloor 10 I \rfloor)$, 20 in all, sampled at every
change of the best quotes. From consecutive samples, per state: $Q_{xy}$ the probability of moving
to $y$ with the mid unchanged, $T_{xy}$ with the mid moved, $R_x$ the expected mid change. Each
transition is also counted mirrored ($I \to 1 - I$, $\Delta m \to -\Delta m$), so the estimate is
symmetric. The adjustment

$$G = \sum_{k=1}^{8} g_k,\qquad g_1 = (1 - Q)^{-1} R,\qquad g_{k+1} = (1 - Q)^{-1} T\,g_k$$

is the expected sum of future mid moves from each state; the micro-price is $m + G(x)$. Linear
systems by Gauss-Jordan with partial pivoting; a state never seen gets $G = 0$. Scored by the
reduction in mean squared error against the mid at the next mid change and 1 s later.

Code: `Counts`, `adjustment`, `solve`. Tests: `MicroPrice.*`.

## Queue position (Moallemi and Yuan 2017)

Orders joining the best quote at a 1-tick spread, bucketed by the shares ahead over $D$
(0-0.25, 0.25-0.5, 0.5-1, 1-2, 2+). Per bucket: fill rate (at least one execution before any
cancel, delete or replace), mean wait to the first fill, and the 1 s markout per share in ticks,
$\pm(m_{t+1\text{s}} - p)$ signed by the order's side. Value per order = fill rate x markout. A
rebate of $r$ dollars per share adds $100 r$ ticks to the markout.

## Order lifetimes (Hasbrouck and Saar 2009)

Every order added in the window, followed by reference: cancelled (delete, or replace, whose
replacement counts as a new order) by lifetime bin (1 ms, 10 ms, 100 ms, 1 s, 2 s edges), executed
at least once, or still resting at the end. Fleeting = cancelled within 2 s.

## Trade costs and execution quality (SEC Rule 605)

Each displayed execution is signed by its taker: $d = +1$ if the resting order sold. With $m_0$ the
mid just before the trade, $m_5$ the mid in force 5 s later and $p$ the price:

$$\text{effective} = d\,(p - m_0),\quad \text{realized} = d\,(p - m_5),\quad \text{impact} = d\,(m_5 - m_0),$$

so effective = realized + impact exactly. Averages are weighted by shares, in basis points of
$m_0$ (`itch_study`) or cents per share (`itch_bestex`). Size buckets: 1-100, 101-500, 501-2,000,
over 2,000 shares. Hidden executions strictly inside the quote count as price improvement:
$a - p$ for a buyer, $p - b$ for a seller, half the spread at the mid. Nasdaq marks every hidden
execution as a buy, so its side is inferred from $p$ against $m_0$; one at the mid is unsigned
and adds no spread term.

Code: `src/surveil/best_ex.hpp`. Tests: `BestEx.SplitsTheEffectiveSpreadAndCountsPriceImprovement`,
`BestEx.SignsTakerSells`.

## Volatility

- **Realized variance with subsampling.** From the mid at each whole second $m_i$, for sampling
  period $k$ seconds:
  $$\mathrm{RV}(k) = \frac1k \sum_{o=0}^{k-1} \sum_j \bigl(\log m_{o + jk} - \log m_{o + (j-1)k}\bigr)^2,$$
  the average over the $k$ sampling offsets (Zhang, Mykland and Ait-Sahalia 2005). The signature
  ratio $\mathrm{RV}(k)/\mathrm{RV}(300)$ is 1 for a martingale without noise; bid-ask bounce pushes
  it above 1 at small $k$, trending pushes it below (Andersen, Bollerslev, Diebold and Labys 2000).
  With 66 returns at 300 s its relative standard error is about $\sqrt{2/66} \approx 17\%$.
- **Eta** (Robert and Rosenbaum 2011): over non-zero mid changes, $\eta = N_c / (2 N_a)$ with $N_c$
  continuations and $N_a$ alternations; $\eta < 1/2$ means mean reversion at the tick scale.
  Code: `EtaCounter` in `src/strategy/tick_stats.hpp`.
- **Online variance rate** (market makers). An exponential average of $\Delta m^2 / \Delta t$ in which
  each update covers the time since the previous one with weight $1 - e^{-\Delta t / \tau}$:
  $$\sigma^2 \leftarrow \sigma^2 + (1 - e^{-\Delta t/\tau})\,(\Delta m^2/\Delta t - \sigma^2).$$
  For small steps the weight is $\Delta t / \tau$, so the estimate tends to $\sum \Delta m^2 / \sum \Delta t$
  however often it is called. Test: `Strategies.AvellanedaStoikovVarianceIgnoresTheCallRate`.

## VPIN (Easley, Lopez de Prado and O'Hara 2012)

Executions with exact signs fill $B = 500$ buckets of equal volume $V$ per symbol-day (a trade
spills over bucket boundaries). Bucket imbalance $|V^{buy} - V^{sell}| / V$; VPIN is its mean over
the last 50 buckets. Following Andersen and Bondarenko (2014), its correlation with the next
bucket's absolute log mid return is compared with that of the current bucket: a link to the
current bucket only is mechanical, not predictive. Correlations are $\operatorname{sign}(\hat\beta)\sqrt{R^2}$
of the OLS fit.

Code: `volume_buckets`. Test: `Vpin.BucketsSplitTradesAndMeasureImbalance`.

## Hawkes self-excitation (Filimonov and Sornette 2012)

Mid-change times $t_1 < \dots < t_n$ in a window $[0, T]$, intensity
$\lambda(t) = \mu + \alpha \sum_{t_i < t} e^{-\beta (t - t_i)}$. Log-likelihood, with the recursion
$A_1 = 0$, $A_i = e^{-\beta (t_i - t_{i-1})}(1 + A_{i-1})$ (Ozaki 1979):

$$\ell = \sum_i \log(\mu + \alpha A_i) - \mu T - \frac{\alpha}{\beta}\sum_i \bigl(1 - e^{-\beta (T - t_i)}\bigr).$$

Maximised by Nelder-Mead over $(\log\mu, \log\alpha, \log\beta)$ from three starting memories,
restricted to $\alpha < \beta$ (stationary). Branching ratio $n = \alpha / \beta$: the expected
number of events each event triggers, so the share of triggered events in a stationary process.
Fitted on half-hour windows with at least 200 changes, which limits seasonality that would
otherwise read as excitation.

Code: `hawkes_loglik`, `fit_hawkes`. Tests: `Hawkes.MaximumLikelihoodRecoversASimulatedProcess`,
`Hawkes.APoissonProcessHasNoExcitation`.

## Models

Closed-form models drawn in the studio's Models tab (`studio/static/models.js`); each is checked
against the identities below by `studio/test_models.mjs` (`Studio.Models`).

- **Kyle (1985).** Value $v \sim N(p_0, \sigma_v^2)$, noise order flow $u \sim N(0, \sigma_u^2)$. The
  insider trades $x = \beta(v - p_0)$ and the market maker sets $p = p_0 + \lambda(x + u)$; in the
  Nash equilibrium $\beta = \sigma_u/\sigma_v$ and $\lambda = \sigma_v/(2\sigma_u)$, so $\lambda\beta = 1/2$:
  the price moves half the way to the value, the insider earns $\sigma_v\sigma_u/2$ in expectation,
  paid by the noise traders, and more noise means less price impact.
- **Glosten and Milgrom (1985).** $V \in \{V_L, V_H\}$ with $P(V_H) = \delta$; a share $\mu$ of traders
  know $V$, the rest buy or sell with probability 1/2. Competitive quotes break even:
  $a = E[V \mid \text{buy}]$, $b = E[V \mid \text{sell}]$, from $P(\text{buy} \mid V_H) = \mu + (1-\mu)/2$ and
  $P(\text{buy} \mid V_L) = (1-\mu)/2$. The spread is zero with no informed traders, $V_H - V_L$ when
  all are informed, and grows with $\mu$; after each trade $\delta$ is updated by Bayes' rule, so the
  quotes converge to the value and the spread narrows.
- **Almgren and Chriss (2000).** Selling $X$ shares over $N$ intervals of $\tau = T/N$ with volatility
  $\sigma$, temporary impact $\eta$, permanent impact $\gamma$ and risk aversion $\lambda$: with
  $\tilde\eta = \eta - \gamma\tau/2$ and $\cosh(\kappa\tau) = 1 + \lambda\sigma^2\tau^2/(2\tilde\eta)$, holdings are
  $x_j = X \sinh(\kappa(T - t_j))/\sinh(\kappa T)$; expected cost
  $\tfrac12\gamma X^2 + (\tilde\eta/\tau)\sum n_j^2$ and variance $\sigma^2\tau\sum x_j^2$, with $n_j$ the
  shares sold in interval $j$. $\lambda = 0$ gives the straight line; raising $\lambda$ sells earlier,
  costs more and risks less, which traces the efficient frontier.
- **Avellaneda and Stoikov (2008)**, as above: reservation price $r = s - q\gamma\sigma^2(T - t)$ and
  total spread $\gamma\sigma^2(T - t) + \tfrac2\gamma\ln(1 + \gamma/k)$ around it.

## Multi-level OFI and cross-impact (Cont, Cucuringu and Zhang 2023)

The best-level order flow $e_n$ above, applied to the $m$-th best prices of each side: for
$m = 1..10$, $e^m_n = q^b_m(n)\,1\{P^b_m(n) \ge P^b_m(n-1)\} - q^b_m(n-1)\,1\{P^b_m(n) \le P^b_m(n-1)\}
- q^a_m(n)\,1\{P^a_m(n) \le P^a_m(n-1)\} + q^a_m(n-1)\,1\{P^a_m(n) \ge P^a_m(n-1)\}$, a missing ask
level counting as an infinitely high price and a missing bid level as zero. Summed over an interval
(10 s by default) and divided by the interval's mean depth per level, this gives a 10-vector of
order flow per interval. The integrated OFI is its projection on the first principal component,
fitted on the morning (09:35 to 12:45) and scaled to unit $L_1$ norm with positive sum. Price impact:
the interval's mid change regressed on the best-level OFI and on the integrated OFI in the morning,
$R^2$ measured on the afternoon. Cross-impact: the next interval's mid change regressed (ridge,
$\lambda = 1$, intercept unpenalised) on this interval's integrated OFI of the symbol alone or of
every symbol, again fitted on the morning and scored on the afternoon. Code: `src/strategy/mlofi.hpp`,
`apps/itch_mlofi.cpp` (`lobiq mlofi`). Tests: `MultiLevelOfi.*`; level 1 reproduces the best-level OFI
case by case.

## Market realism (LOB-Bench statistics)

For a reference symbol-day A and another B, from 09:35 to 15:55: spread in ticks, the bid's share of
best-level size, log time between messages, log time from submission to deletion, log best bid and
ask sizes, depth of new and of deleted limit orders in ticks from the mid, traded volume per minute
in one-second buckets, and best-level OFI per second, the statistics LOB-Bench compares (Nagy et al.
2025). Each pair of samples (at most 200,000 values each, a reservoir sample with a fixed seed) is
compared by total variation between 50-bin histograms whose bins each hold 2% of the pooled
sample (LOB-Bench's $L_1$, halved to lie in $[0, 1]$; equal-width bins over the pooled range let a
few orders thousands of ticks from the mid put the whole bulk in one bin) and by the Wasserstein-1 distance between quantile functions at 1,000
points, divided by A's standard deviation. Stylised facts beside them: messages per minute,
cancellations and executions per new order, the kurtosis and lag-1 autocorrelation of one-minute
returns, and the mean autocorrelation of absolute one-minute returns at lags 1 to 5 (volatility
clustering). Code: `src/strategy/realism.hpp`, `apps/itch_realism.cpp` (`lobiq realism`). Tests:
`Realism.*`.

## LOBSTER export

Each ITCH message becomes the events of LOBSTER's message file (lobsterdata.com): add and add with
attribution are submissions (type 1), a partial cancel a cancellation (2), a delete a deletion (3) of
the remaining shares, an execution with or without price a visible execution (4) at the order's
price, a replace a deletion followed by a submission with the new id, a non-displayed trade a hidden
execution (5), a cross trade type 6 and a trading action a halt indicator (7: price -1 halted or
paused, 0 quoting, 1 trading). After each event, the orderbook file has the top $N$ levels as ask
price, ask size, bid price, bid size, empty levels as 9999999999 or -9999999999 with size 0.
Code: `src/data/lobster.hpp`, `apps/itch_lobster.cpp` (`lobiq lobster`). Tests: `Lobster.*`, and
`tests/lobster_check.py`, which rebuilds the book from the message file alone and must match every
orderbook line.

## Markov chain of the book

The state of the top of the book is $(s, k)$: the spread in ticks, capped at 3, and the bucket of the
bid's share of best-level size $I = Q_b/(Q_b + Q_a)$, $k = \lfloor nI \rfloor$ for $n$ equal buckets
(default 5). This is the state space Stoikov (2018) uses for the micro-price. Over a day (09:35 to
15:55 ET, auctions left out), every book event updates the state; counted are the transitions between
distinct states and, at each change of the mid, its direction from the state just before:
$\hat p_{\uparrow}(x) = N_\uparrow(x) / (N_\uparrow(x) + N_\downarrow(x))$. Fitted from 09:35 to 12:45, the
chain then predicts each mid move of the afternoon as up when $\hat p_\uparrow > 1/2$ (a tie is a miss).
The hit rate is set against always guessing the afternoon's more frequent direction, a baseline that
knows the answer's proportion in advance. Code: `src/strategy/markov.hpp`, `apps/itch_markov.cpp`
(`lobiq markov`, the studio's Prediction tab). Tests: `Markov.*`.

## Portfolio frontier (Markowitz 1952)

For runs $i = 1..n$ with minute PnL changes $r_{i,t}$ over their $T$ common minutes, $\mu$ and
$\Sigma$ are the sample means and covariance. A mix $w$ ($w_i \ge 0$, $\sum w_i = 1$, shares of capital)
has expected day PnL $T\,w^\top\mu$ and risk $\sqrt{T\,w^\top\Sigma w}$ (minutes taken as independent).
The studio samples 4,000 mixes uniformly over the simplex (normalised exponentials, fixed seed) plus
each run alone, and marks the least-risk mix and the best ratio of expected PnL to risk. In sample:
the weights are fitted to the same day they are shown on. Code: `studio/static/app.js` (`lb-frontier`).

## Lead-lag

For each mid move of symbol A, the first move of B strictly after it within 1 s, and whether it
goes the same way (or the opposite, for an inverse pair). Delays are binned at 1 us, 10 us,
100 us, 1 ms, 10 ms, 1 s. The placebo shifts every move of A by 2 s: it keeps slow common drift
and removes fast information, so the edge is the same-direction rate minus the placebo's.

Code: `lead_response`. Test: `LeadLag.AnswersAreTheFollowersNextMoveWithinTheHorizon`.

## Market making

- **Fill intensity** $\Lambda(\delta) = A e^{-k\delta}$ at distance $\delta$ ticks from the mid,
  fitted by Poisson maximum likelihood over exposure time $E_b$ and fills $N_b$ per 1-tick bin:
  $\ell(a, k) = \sum_b N_b (a - k x_b) - E_b e^{a - k x_b}$ with $a = \log A$, concave, solved by
  Newton steps. Exposure and fills decay with time constant $\tau$; a prior worth a fixed exposure
  is never forgotten. Code: `IntensityEstimator`. Tests: `Intensity.*`.
- **Avellaneda-Stoikov (2008).** Reservation price $r = m - q\gamma\sigma^2(T - t)$, half-spread
  $\frac1\gamma\log(1 + \gamma/k) + \frac12\gamma\sigma^2(T - t)$, inventory $q$ in lots.
- **Gueant, Lehalle and Fernandez-Tapia (2013)**, the asymptotic quotes for bounded inventory:
  $$\delta^{b,a}(q) = \frac1\gamma\log\Bigl(1 + \frac\gamma k\Bigr) \pm \frac{2q \pm 1}{2}\,
    \sqrt{\frac{\sigma^2\gamma}{2kA}\Bigl(1 + \frac\gamma k\Bigr)^{1 + k/\gamma}},$$
  with $+(2q+1)$ for the bid and $-(2q-1)$ for the ask. Quotes are rounded away from the mid and
  never cross the opposite best. Code: `AvellanedaStoikov` in `src/backtest/strategies.hpp`.
- **Micro-price maker.** Fair value is the micro-price $P_\mu = (P_b Q_a + P_a Q_b)/(Q_a + Q_b)$
  (see above), shifted against inventory: $r = P_\mu - s\,\tau\,q / n$ for skew $s$ ticks per
  quote size $n$ and tick $\tau$. Quotes are $\lfloor (r - e\tau)/\tau \rfloor\tau$ and
  $\lceil (r + e\tau)/\tau \rceil\tau$ for edge $e$, then held at or behind the best: a quote
  joins or steps back, never improves. With $e = s = 0$ it is the plain joiner. Code:
  `strategies/microprice.cpp`. Test: `Registry.MicroPriceReducesToNaiveWithoutEdgeOrSkew`.
- **Prediction markets.** Avellaneda-Stoikov with a fixed horizon $h$ in logit space,
  $x = \log(p / (1 - p))$, since prices are probabilities in $(0, 1)$: $r = x - q\gamma\sigma^2 h$,
  half-spread $\frac12\gamma\sigma^2 h + \frac1\gamma\log(1 + \gamma/k)$, mapped back through the
  logistic function and rounded away from the mid; $\sigma^2$ is the variance rate of $\Delta x$.
  Code: `TokenMaker` in `src/pm/maker.hpp`; other quoters plug in through `src/pm/quoter.hpp`.

## Backtest fill model

Queue-position replay on the recorded order flow: a simulated order joins behind every real order
resting at its price, and is filled when a recorded execution would have reached it first. Real
orders never react to the simulated ones, which is accurate for small orders and optimistic for
large ones (`diverted_shares` measures how much volume was taken from the recorded market).
Latencies delay market data and order entry separately. PnL = spread capture + inventory PnL +
fees, checked after every event. Details: docs/STRATEGIES.md.

## Evaluation

- **Walk-forward.** Every parameter is chosen on train days and scored unchanged on later test
  days, including one six months later and one six years earlier.
- **Medians with bootstrap intervals.** Medians over symbols with 95% percentile intervals from
  2,000 resamples of symbols (fixed seed). A median over an even count averages the middle pair.
  Code: `median` in `microstructure.hpp`, `median_ci` in `apps/itch_study.cpp`. Test:
  `Median.AveragesTheMiddlePairAndSkipsNaN`.
- **Paired comparisons.** Strategy against baseline per symbol-day, summed, with 95% intervals
  from 10,000 resamples of symbol-days (`scripts/maker_walkforward.sh`,
  `scripts/leader_walkforward.sh`). Pairing removes the day and symbol effects both share.
- **Risk-adjusted backtest metrics.** On one-minute PnL changes $r_1..r_T$ (from `pnl_minutes.csv`):
  Sharpe $\widehat{SR} = \bar r / s_r$, annualised by $\sqrt{252 \cdot 390}$; maximum drawdown from
  the running peak (the day starts at 0); profit factor; hit rate over non-zero minutes.
- **Probabilistic Sharpe ratio (Bailey and Lopez de Prado 2012).**
  $PSR(SR^*) = \Phi\big((\widehat{SR} - SR^*)\sqrt{T-1} / \sqrt{1 - \gamma_3 \widehat{SR} + \tfrac{\gamma_4 - 1}{4}\widehat{SR}^2}\big)$,
  with $\gamma_3$ the skewness and $\gamma_4$ the (non-excess) kurtosis of $r$; $PSR(0)$ is reported.
- **Deflated Sharpe ratio (Bailey and Lopez de Prado 2014).** For the $N$ runs of one sweep, the
  hurdle is the best Sharpe expected from luck,
  $SR_0 = \sqrt{V[\widehat{SR}_n]}\,\big((1-\gamma)\Phi^{-1}(1 - 1/N) + \gamma\,\Phi^{-1}(1 - 1/(Ne))\big)$ with
  $\gamma$ the Euler-Mascheroni constant, and $DSR = PSR(SR_0)$. A sweep's best run with a high PSR
  but a low DSR is what the number of trials alone would produce. One symbol's runs in one sweep
  are one set of trials. $\Phi^{-1}$ uses Acklam's approximation (relative error below $1.2 \times 10^{-9}$).
  Code: `src/backtest/metrics.hpp` (`itch_backtest`, `summary.csv`, sweep tables) and, independently,
  `studio/metrics.py`. Tests: `Metrics.*` (values pinned from the Python implementation) and
  `Studio.EndToEnd`, which requires both to agree on a real sweep.
- **Block bootstrap of the day's PnL.** Circular block bootstrap of the minute PnL changes, blocks
  of $\mathrm{round}(T^{1/3})$ minutes, 2,000 resamples with a fixed seed: the 2.5% and 97.5%
  quantiles of the summed PnL and the share of resamples below zero. Code: `block_bootstrap` in
  `studio/metrics.py`.
- **Superior predictive ability** (Hansen 2005). For the $K$ runs of a sweep on one symbol, $d_{k,t}$ is
  run $k$'s minute PnL over not trading. $T_{SPA} = \max(\max_k \sqrt n\,\bar d_k / \hat\omega_k, 0)$ with $\hat\omega_k$ from
  1,000 stationary-bootstrap resamples (Politis and Romano 1994; mean block 10 minutes, fixed seed).
  The null distribution recentres each run at $\bar d_k \mathbf 1\{\sqrt n\,\bar d_k/\hat\omega_k < -\sqrt{2\log\log n}\}$ (consistent
  p-value: only clearly worse runs keep their negative mean); recentring every run at zero excess
  gives White's reality check, the upper bound. A small p-value says the best run beats not trading by
  more than the search over $K$ runs explains.
- **Model confidence set** (Hansen, Lunde and Nason 2011) on minute losses $-d_{k,t}$ with the $T_{\max}$
  statistic: repeatedly drop the run with the largest studentised excess loss, each time with its
  bootstrap p-value; a run's MCS p-value is the largest p-value up to its removal. `mcs_p` in the sweep
  table; the runs with $p \ge 0.1$ form the 90% set.
- **Probability of backtest overfitting** (Bailey, Borwein, Lopez de Prado and Zhu 2017). The day's
  minutes are cut into 10 blocks; for each of the 252 ways to pick 5 as in-sample, the run with the
  best in-sample Sharpe is ranked among all runs out of sample, $\omega \in (0, 1)$; PBO is the share of splits
  with $\omega \le 1/2$. Code: `src/backtest/overfit.hpp`, printed by every sweep and in its report.
  Tests: `StationaryBootstrap.*`, `Spa.*`, `ModelConfidenceSet.*`, `Pbo.*`.

## Volatility forecasts

Mid changes $r_t$ in ticks over a fixed interval (`--interval`, default 10 s), 09:35 to 15:55 ET.

- **GARCH(1,1)** (Bollerslev 1986): $h_{t+1} = \omega + \alpha r_t^2 + \beta h_t$, $r_t = \sqrt{h_t}\,z_t$ with
  $z_t$ standard normal or standardised Student-t with $\nu$ degrees of freedom (Bollerslev 1987):
  $\log f(z) = \log\Gamma(\tfrac{\nu+1}2) - \log\Gamma(\tfrac\nu2) - \tfrac12\log(\pi(\nu-2)) - \tfrac{\nu+1}2\log(1 + \tfrac{z^2}{\nu-2})$.
  Maximum likelihood by Nelder-Mead on $r / \mathrm{sd}(r)$, with $h_0$ the sample variance and
  $\alpha + \beta = 0.9999\,\sigma(p_1)$, $\alpha = (\alpha + \beta)\,\sigma(p_2)$, $\nu = 2 + e^{p_3}$ ($\sigma$ logistic), so
  every trial point is stationary. The $k$-step forecast is $\bar h + (\alpha+\beta)^{k-1}(h_{t+1} - \bar h)$
  with $\bar h = \omega / (1 - \alpha - \beta)$; half-life $\log\tfrac12 / \log(\alpha + \beta)$ intervals.
- **HAR** (Corsi 2009) on per-minute realized variance $RV_m = \sum r_t^2$:
  $RV_{m+1} = b_0 + b_1 RV_m + b_2 \overline{RV}_{m-4..m} + b_3 \overline{RV}_{m-29..m}$ by least squares.
- **Evaluation.** Fitted on the train days and scored unchanged on the test days against the EWMA
  the makers use ($\tau = 60$ s), with Patton's (2011) robust loss $\mathrm{QLIKE}(RV, f) = \log f + RV / f$,
  which ranks forecasts as the true variance would when $RV$ is a noisy proxy. The Diebold and
  Mariano (1995) statistic is the mean loss difference over its Newey-West standard error.
- **Quoting.** `avellaneda_stoikov garch=1 garch_omega=.. garch_alpha=.. garch_beta=.. garch_dt_s=..`
  replaces $\sigma^2$ by the one-step GARCH forecast per second, $h_{t+1} / \Delta t$, updated at each
  grid boundary from the last mid seen before it; `lobiq vol` prints the keys.

Code: `src/strategy/garch.hpp`, `apps/itch_vol.cpp`. Tests: `Garch.*`, `Har.*`, `Qlike.*`, `DieboldMariano.*`, `NelderMead.*`.

## Multivariate Hawkes

Six event types at the best quotes (`apps/itch_hawkes.cpp`): market buys and sells (executions,
one per timestamp), limit orders joining or improving the bid or the ask, cancels at the bid or the
ask. Intensities

$$\lambda_i(t) = \mu_i + \sum_j a_{ij} \sum_{t_k < t,\ d_k = j} \beta e^{-\beta (t - t_k)},$$

so $a_{ij}$ is the expected number of type-$i$ events one type-$j$ event triggers; log-likelihood
$\sum_k \log\lambda_{d_k}(t_k) - T\sum_i \mu_i - \sum_k \bigl(\sum_i a_{i d_k}\bigr)(1 - e^{-\beta(T - t_k)})$, summed over windows.

- **EM** (Lewis and Mohler 2011) at fixed $\beta$: each event's probability of being background,
  $\mu_i / \lambda_i(t_k)$, or triggered by type $j$, $a_{ij} R_j(t_k) / \lambda_i(t_k)$ with $R_j$ the decayed
  kernel sum; then $\mu_i$ = expected background events / $T$ and $a_{ij}$ = expected $j$-triggered type-$i$
  events / $\sum_{d_k = j}(1 - e^{-\beta(T - t_k)})$. Each pass is $O(N D^2)$ by the exponential recursion and
  never lowers the likelihood. $\beta$: best of a log grid, refined by golden-section search.
- **Branching ratio**: the spectral radius of $(a_{ij})$ (power iteration on $I + a$, which removes
  periodicity); below 1 the process is stationary and it is the share of triggered events.
- **Out of sample**: test-window log-likelihood per event over a Poisson process with the train
  rates. **Time rescaling** (Brown et al. 2002): the compensator increments between consecutive events
  of each type are Exp(1) under the true model; the Kolmogorov-Smirnov distance from Exp(1) is reported.
- **Simulation**: Ogata's (1981) thinning, with the total intensity just after the last event as the bound.

Code: `src/strategy/hawkes_mv.hpp`. Tests: `HawkesMv.*` (including agreement with the univariate likelihood above).

## Regimes

Per symbol-minute (`apps/itch_regime.cpp`): $\log(RV + 10^{-4})$ of 1 s mid changes, log mean spread,
$\log(1 + \text{trades})$ and $\log(1 + |\mathrm{OFI}| / \text{depth})$, standardised per symbol on the train days.

- **Gaussian HMM** with diagonal covariances, started from k-means++ (Arthur and Vassilvitskii 2007),
  fitted by Baum-Welch with scaled forward-backward passes (Rabiner 1989) over every train symbol-day;
  variances floored at $10^{-3}$. The number of states (2 to 4) minimises
  $\mathrm{BIC} = -2\ell + p \log n$, $p = (K - 1) + K(K - 1) + 2KF$.
- **Causal labels**: test minutes take the state of largest forward probability
  $P(s_t \mid x_{1..t})$, which uses nothing after $t$ (test `Hmm.FilterIsCausal`); states are ordered by
  their mean log realized variance. Each regime is described by the next minute's realized variance and
  absolute mid change, and `--pnl` splits a backtest's minute PnL by the regime it was earned in.

Code: `src/strategy/regime.hpp`. Tests: `KMeans.*`, `Hmm.*`.

## Conformal intervals

- **Split conformal** (Vovk et al. 2005; Lei et al. 2018): with calibration scores $s_i = |y_i - \hat y_i|$,
  $i = 1..n$, the half-width is the $\lceil (n + 1)(1 - \alpha) \rceil$-th smallest score; a new exchangeable
  point lands inside with probability at least $1 - \alpha$.
- **Adaptive conformal inference** (Gibbs and Candes 2021): the working level moves after each outcome,
  $\alpha_{t+1} = \alpha_t + \gamma(\alpha - \mathrm{miss}_t)$, and the half-width is the split quantile at $\alpha_t$ over a
  sliding window of scores; the long-run miss rate stays near $\alpha$ under any distribution shift.
- **`conformal` strategy**: the micro-price $P_\mu$ forecasts the mid $h$ seconds ahead; every
  `sample_s` a forecast is issued with the current half-width $q_t$ and scored $h$ later. Quotes are
  $\lfloor r - \max(q_t, e) \rfloor$ and $\lceil r + \max(q_t, e) \rceil$ ticks with $r = P_\mu - s\,q/n$ for skew $s$,
  inventory $q$ and quote size $n$; post-only.

Code: `src/strategy/conformal.hpp`, `strategies/conformal.cpp`. Tests: `SplitConformal.*`, `AdaptiveConformal.*`.

## Alpha model

`apps/itch_alpha.cpp`: the 23 streaming features of `SymbolFeatures`, exactly as a strategy sees them,
sampled every `--step` seconds; target the mid change over the next `--horizon` seconds.

- **Ridge** with an unpenalised intercept on features standardised on the training set and clipped
  at $\pm 5$ (missing values at the mean); penalty $\lambda n$ with $\lambda \in \{10^{-4}, \dots, 10\}$.
- **Purged, embargoed k-fold** (Lopez de Prado 2018): contiguous test folds; a training sample is kept
  only if its label ends before the fold's first sample or it starts more than the embargo (one
  horizon) after the fold's last label ends, so no training label overlaps test information. Each
  fold standardises on its own training samples. $\lambda$ maximises out-of-fold $R^2$.
- **Interval**: split-conformal half-width from the out-of-fold residuals at the chosen $\lambda$.
- **Test days**: $R^2$, information coefficient (Pearson correlation of forecast and outcome), sign hit
  rate over non-zero moves, interval coverage, and the move caught by the most confident tenth of
  forecasts against the median half spread. `--model DIR` writes the scaler and coefficients;
  `alpha:<FILE>` shifts its reservation price by `shade` times the forecast and, with `taking`,
  crosses when the forecast exceeds half the spread plus `fee_ticks` and `take_margin`.

Code: `apps/itch_alpha.cpp`, `strategies/alpha.cpp`, `purged_kfold` in `src/backtest/overfit.hpp`. Tests: `PurgedKFold.*`, `Research.SyntheticDays`.

## Symbolic regression

`itch_alpha --symbolic G` searches formulas over the same standardised features, the method of PySR
(Cranmer 2023) without its Julia engine. Programs are postfix arrays over $+, -, \times$, protected
division ($a / b$, or 0 when $|b| < 10^{-6}$), negation and $\tanh$, at most 25 nodes; a population of
400 starts ramped half-and-half (Koza 1992) and evolves for $G$ generations by tournament selection
(7, ties to the smaller program), subtree crossover, subtree and constant mutation, with elitism.
Every candidate is linearly scaled, $\hat y = a + b f(x)$ by least squares (Keijzer 2003), so the search
looks for shape, not constants. The best formula of each size is kept; sizes that fit better than every
smaller one form the accuracy-complexity front. The search fits the first 80% of the train samples
(thinned to 20,000, purged of labels reaching past the split); on the last 20% the smallest front member
whose validation error is within 0.5% of the best is chosen, then scored on the test days. Evaluation
runs a whole column per node over buffers allocated once.

Code: `src/strategy/symreg.hpp`. Tests: `SymReg.*` (a planted interaction is recovered at its size).

## Options

- **Black-Scholes-Merton** (Black and Scholes 1973) with dividend yield $q$:
  $d_{1,2} = (\log(S/K) + (r - q \pm \sigma^2/2)T) / (\sigma\sqrt T)$, call $S e^{-qT}N(d_1) - K e^{-rT}N(d_2)$; delta,
  gamma, vega, theta, rho, vanna $= -e^{-qT}\varphi(d_1) d_2/\sigma$ and volga $= \text{vega}\, d_1 d_2/\sigma$.
  **Digital** (cash-or-nothing) call $e^{-rT}N(d_2)$, which is $-\partial C/\partial K$: a prediction-market
  share on "above $K$ at $T$".
- **Implied volatility**: Newton steps on vega from the Brenner-Subrahmanyam start, kept inside a
  bisection bracket; NaN outside the no-arbitrage bounds.
- **Binomial tree** (Cox, Ross and Rubinstein 1979): $u = e^{\sigma\sqrt{\Delta t}}$, $d = 1/u$,
  $p = (e^{(r-q)\Delta t} - d)/(u - d)$; American values take $\max(\text{continuation}, \text{exercise})$ at every node;
  delta and gamma from the first two steps.
- **Itô's lemma and Monte Carlo.** For $dS = \mu S\,dt + \sigma S\,dW$, Itô's lemma on $f = \log S$ gives
  $d\log S = (\mu - \tfrac12\sigma^2)dt + \sigma\,dW$: the $-\tfrac12\sigma^2$ is the second-order term
  $\tfrac12 f''(S)\,\sigma^2 S^2$. So $S_T = S\exp((\mu - \tfrac12\sigma^2)T + \sigma\sqrt T Z)$ exactly, and $E[\log S_T]$
  lies $\tfrac12\sigma^2 T$ below $\log E[S_T]$ (test `MonteCarlo.ItoDriftCorrection`). The same lemma on
  $x = \log(p/(1-p))$ is why the prediction-market quoter works in logit units. European prices under
  $\mu = r - q$ with antithetic pairs and $S_T$ as control variate ($E[S_T] = S e^{(r-q)T}$); pathwise delta
  $e^{-rT}\mathbf 1\{S_T > K\} S_T / S$ for a call.
- **SVI surface** (Gatheral 2004): total variance per expiry
  $w(k) = a + b(\rho(k - m) + \sqrt{(k - m)^2 + s^2})$ at $k = \log(K/F)$, fitted by vega-weighted least squares
  in $w$ with $b > 0$, $|\rho| < 1$, $s > 0$ and $a + b s\sqrt{1 - \rho^2} \ge 0$. The forward is the median over
  strikes of $K + e^{rT}(C - P)$ (put-call parity), and implied volatilities come from out-of-the-money
  mids with Black-76. Butterfly arbitrage where Durrleman's
  $g(k) = (1 - k w'/(2w))^2 - \tfrac{w'^2}4(\tfrac1w + \tfrac14) + \tfrac{w''}2 < 0$ (Gatheral and Jacquier 2014); calendar
  arbitrage where $w$ falls with expiry at fixed $k$. Between expiries $w$ is linear in $T$.

Code: `src/options/`, `apps/options.cpp`. Tests: `BlackScholes.*`, `ImpliedVol.*`, `Digital.*`, `Binomial.*`, `MonteCarlo.*`, `Svi.*`, `Options.SyntheticChain`.

## Surveillance rules

Window 09:35 to 15:50 ET, outside the auctions. $D$ is the typical depth above.

- **Spoofing:** an order at the touch with at least $\max(5{,}000, 10D)$ shares, cancelled
  unfilled within 2 s; ranked by shares executed on the opposite side while it showed.
- **Layering:** within 50 ms, at least 4 same-side orders from one attributed MPID within 10 ticks of
  the best, on at least 3 price levels, together at least $\max(5{,}000, 10D)$ shares, each
  cancelled unfilled within 2 s.
- **Quote stuffing:** a 100 ms bucket with at least 1,000 messages and 50 times the symbol's median
  busy bucket, executions at most 1% and cancels at least 40% of its messages; adjacent buckets merge.

Code: `src/surveil/surveil.hpp`. Tests: `Surveil.*`.

## References

- Andersen, T., Bollerslev, T., Diebold, F. and Labys, P. (2000). Great realizations. *Risk* 13.
- Andersen, T. and Bondarenko, O. (2014). VPIN and the flash crash. *Journal of Financial Markets* 17.
- Avellaneda, M. and Stoikov, S. (2008). High-frequency trading in a limit order book. *Quantitative Finance* 8(3).
- Bailey, D. and Lopez de Prado, M. (2012). The Sharpe ratio efficient frontier. *Journal of Risk* 15(2).
- Bailey, D. and Lopez de Prado, M. (2014). The deflated Sharpe ratio. *Journal of Portfolio Management* 40(5).
- Cont, R., Kukanov, A. and Stoikov, S. (2014). The price impact of order book events. *Journal of Financial Econometrics* 12(1).
- Easley, D., Lopez de Prado, M. and O'Hara, M. (2012). Flow toxicity and liquidity in a high-frequency world. *Review of Financial Studies* 25(5).
- Filimonov, V. and Sornette, D. (2012). Quantifying reflexivity in financial markets. *Physical Review E* 85.
- Gueant, O., Lehalle, C.-A. and Fernandez-Tapia, J. (2013). Dealing with the inventory risk. *Mathematics and Financial Economics* 7(4).
- Markowitz, H. (1952). Portfolio selection. *Journal of Finance* 7(1).
- Almgren, R. and Chriss, N. (2000). Optimal execution of portfolio transactions. *Journal of Risk* 3(2).
- Glosten, L. and Milgrom, P. (1985). Bid, ask and transaction prices in a specialist market with heterogeneously informed traders. *Journal of Financial Economics* 14(1).
- Kyle, A. (1985). Continuous auctions and insider trading. *Econometrica* 53(6).
- Cont, R., Cucuringu, M. and Zhang, C. (2023). Cross-impact of order flow imbalance in equity markets. *Quantitative Finance* 23(10).
- Nagy, P. et al. (2025). LOB-Bench: benchmarking generative AI for finance, an application to limit order book data. arXiv:2502.09172.
- Hasbrouck, J. and Saar, G. (2009). Technology and liquidity provision. *Journal of Financial Markets* 12(2).
- Moallemi, C. and Yuan, K. (2017). A model for queue position valuation in a limit order book. Working paper, Columbia University.
- Ozaki, T. (1979). Maximum likelihood estimation of Hawkes' self-exciting point processes. *Annals of the Institute of Statistical Mathematics* 31.
- Robert, C. and Rosenbaum, M. (2011). A new approach for the dynamics of ultra-high-frequency data: the model with uncertainty zones. *Journal of Financial Econometrics* 9(2).
- Stoikov, S. (2018). The micro-price: a high-frequency estimator of future prices. *Quantitative Finance* 18(12).
- Zhang, L., Mykland, P. and Ait-Sahalia, Y. (2005). A tale of two time scales. *Journal of the American Statistical Association* 100(472).
- Arthur, D. and Vassilvitskii, S. (2007). k-means++: the advantages of careful seeding. *SODA*.
- Bailey, D., Borwein, J., Lopez de Prado, M. and Zhu, Q. (2017). The probability of backtest overfitting. *Journal of Computational Finance* 20(4).
- Black, F. and Scholes, M. (1973). The pricing of options and corporate liabilities. *Journal of Political Economy* 81(3).
- Bollerslev, T. (1986). Generalized autoregressive conditional heteroskedasticity. *Journal of Econometrics* 31(3).
- Bollerslev, T. (1987). A conditionally heteroskedastic time series model for speculative prices and rates of return. *Review of Economics and Statistics* 69(3).
- Brown, E., Barbieri, R., Ventura, V., Kass, R. and Frank, L. (2002). The time-rescaling theorem and its application to neural spike train data analysis. *Neural Computation* 14(2).
- Corsi, F. (2009). A simple approximate long-memory model of realized volatility. *Journal of Financial Econometrics* 7(2).
- Cox, J., Ross, S. and Rubinstein, M. (1979). Option pricing: a simplified approach. *Journal of Financial Economics* 7(3).
- Cranmer, M. (2023). Interpretable machine learning for science with PySR and SymbolicRegression.jl. arXiv:2305.01582.
- Diebold, F. and Mariano, R. (1995). Comparing predictive accuracy. *Journal of Business and Economic Statistics* 13(3).
- Gatheral, J. (2004). A parsimonious arbitrage-free implied volatility parameterization. Global Derivatives conference.
- Gatheral, J. and Jacquier, A. (2014). Arbitrage-free SVI volatility surfaces. *Quantitative Finance* 14(1).
- Gibbs, I. and Candes, E. (2021). Adaptive conformal inference under distribution shift. *NeurIPS*.
- Hansen, P. (2005). A test for superior predictive ability. *Journal of Business and Economic Statistics* 23(4).
- Hansen, P., Lunde, A. and Nason, J. (2011). The model confidence set. *Econometrica* 79(2).
- Keijzer, M. (2003). Improving symbolic regression with interval arithmetic and linear scaling. *EuroGP*.
- Koza, J. (1992). *Genetic Programming*. MIT Press.
- Lei, J., G'Sell, M., Rinaldo, A., Tibshirani, R. and Wasserman, L. (2018). Distribution-free predictive inference for regression. *JASA* 113(523).
- Lewis, E. and Mohler, G. (2011). A nonparametric EM algorithm for multiscale Hawkes processes. *Journal of Nonparametric Statistics* 1(1).
- Lopez de Prado, M. (2018). *Advances in Financial Machine Learning*. Wiley.
- Ogata, Y. (1981). On Lewis' simulation method for point processes. *IEEE Transactions on Information Theory* 27(1).
- Patton, A. (2011). Volatility forecast comparison using imperfect volatility proxies. *Journal of Econometrics* 160(1).
- Politis, D. and Romano, J. (1994). The stationary bootstrap. *JASA* 89(428).
- Rabiner, L. (1989). A tutorial on hidden Markov models and selected applications in speech recognition. *Proceedings of the IEEE* 77(2).
- Vovk, V., Gammerman, A. and Shafer, G. (2005). *Algorithmic Learning in a Random World*. Springer.
- SEC Rule 605 of Regulation NMS, 17 CFR 242.605.

Engineering references: docs/exec/REFS.md.
