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
- Cont, R., Kukanov, A. and Stoikov, S. (2014). The price impact of order book events. *Journal of Financial Econometrics* 12(1).
- Easley, D., Lopez de Prado, M. and O'Hara, M. (2012). Flow toxicity and liquidity in a high-frequency world. *Review of Financial Studies* 25(5).
- Filimonov, V. and Sornette, D. (2012). Quantifying reflexivity in financial markets. *Physical Review E* 85.
- Gueant, O., Lehalle, C.-A. and Fernandez-Tapia, J. (2013). Dealing with the inventory risk. *Mathematics and Financial Economics* 7(4).
- Hasbrouck, J. and Saar, G. (2009). Technology and liquidity provision. *Journal of Financial Markets* 12(2).
- Moallemi, C. and Yuan, K. (2017). A model for queue position valuation in a limit order book. Working paper, Columbia University.
- Ozaki, T. (1979). Maximum likelihood estimation of Hawkes' self-exciting point processes. *Annals of the Institute of Statistical Mathematics* 31.
- Robert, C. and Rosenbaum, M. (2011). A new approach for the dynamics of ultra-high-frequency data: the model with uncertainty zones. *Journal of Financial Econometrics* 9(2).
- Stoikov, S. (2018). The micro-price: a high-frequency estimator of future prices. *Quantitative Finance* 18(12).
- Zhang, L., Mykland, P. and Ait-Sahalia, Y. (2005). A tale of two time scales. *Journal of the American Statistical Association* 100(472).
- SEC Rule 605 of Regulation NMS, 17 CFR 242.605.

Engineering references: docs/exec/REFS.md.
