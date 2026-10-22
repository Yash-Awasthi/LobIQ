// Classic market-microstructure models, closed form (docs/METHODS.md "Models"). Pure functions: the
// studio's Models tab draws them, studio/test_models.mjs checks them against known identities.
'use strict';
(() => {
  // Kyle (1985), one auction: value v ~ N(p0, sigmaV^2), noise order flow u ~ N(0, sigmaU^2). The
  // insider trades x = beta (v - p0), the market maker prices p = p0 + lambda (x + u); in the Nash
  // equilibrium each is the best response to the other.
  function kyle(sigmaV, sigmaU) {
    const beta = sigmaU / sigmaV, lambda = sigmaV / (2 * sigmaU);
    return {beta, lambda, insiderProfit: sigmaV * sigmaU / 2, posteriorVarShare: 0.5, noiseLoss: sigmaV * sigmaU / 2};
  }

  // Glosten-Milgrom (1985): V is high or low, P(high) = delta; a share mu of traders know V, the rest
  // buy or sell at random. Competitive (zero-profit) quotes are conditional expectations.
  function gmQuotes(vLow, vHigh, delta, mu) {
    const buyH = mu + (1 - mu) / 2, buyL = (1 - mu) / 2;  // P(buy | V high), P(buy | V low)
    const pHbuy = delta * buyH / (delta * buyH + (1 - delta) * buyL);
    const pHsell = delta * buyL / (delta * buyL + (1 - delta) * buyH);
    const ask = vLow + (vHigh - vLow) * pHbuy, bid = vLow + (vHigh - vLow) * pHsell;
    return {ask, bid, spread: ask - bid, pHbuy, pHsell};
  }

  // A path of trades when V is high: after each trade the market maker updates delta by Bayes' rule.
  function gmPath(vLow, vHigh, delta0, mu, trades, seed = 7) {
    let delta = delta0, s = seed;
    const rnd = () => (s = s * 16807 % 2147483647) / 2147483647;
    const out = [];
    for (let i = 0; i < trades; ++i) {
      const q = gmQuotes(vLow, vHigh, delta, mu);
      const buy = rnd() < mu + (1 - mu) / 2;  // V is high: informed traders buy
      out.push({...q, buy, delta});
      delta = buy ? q.pHbuy : q.pHsell;
    }
    return out;
  }

  // Almgren-Chriss (2000), discrete: sell X shares over N intervals of length T/N with volatility
  // sigma (per unit time), temporary impact eta, permanent impact gamma, risk aversion lambda.
  // Holdings follow x_j = X sinh(kappa (T - t_j)) / sinh(kappa T); lambda = 0 is a straight line.
  function almgrenChriss(X, T, N, sigma, eta, gamma, lambda) {
    const tau = T / N, etaT = eta - gamma * tau / 2;
    const k2 = lambda * sigma * sigma / etaT;
    const kappa = k2 > 0 ? Math.acosh(k2 * tau * tau / 2 + 1) / tau : 0;
    const x = Array.from({length: N + 1}, (_, j) => kappa > 0 ? X * Math.sinh(kappa * (T - j * tau)) / Math.sinh(kappa * T) : X * (1 - j / N));
    let cost = gamma * X * X / 2, variance = 0;
    for (let j = 1; j <= N; ++j) {
      const n = x[j - 1] - x[j];
      cost += etaT / tau * n * n;
      variance += sigma * sigma * tau * x[j] * x[j];
    }
    return {kappa, holdings: x, cost, variance, sd: Math.sqrt(variance)};
  }

  // The efficient frontier: expected cost against risk as risk aversion sweeps several decades.
  function acFrontier(X, T, N, sigma, eta, gamma, lambdas) {
    return lambdas.map(l => ({lambda: l, ...almgrenChriss(X, T, N, sigma, eta, gamma, l)}));
  }

  // Avellaneda-Stoikov (2008): reservation price and optimal total spread for inventory q at time t.
  function avellanedaStoikov(s, q, gamma, sigma, k, T, t) {
    const r = s - q * gamma * sigma * sigma * (T - t);
    const spread = gamma * sigma * sigma * (T - t) + 2 / gamma * Math.log(1 + gamma / k);
    return {reservation: r, spread, bid: r - spread / 2, ask: r + spread / 2};
  }

  globalThis.LobiqModels = {kyle, gmQuotes, gmPath, almgrenChriss, acFrontier, avellanedaStoikov};
})();
