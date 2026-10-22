// Checks studio/static/models.js against identities each model is known for. Run: node studio/test_models.mjs
import assert from 'node:assert/strict';

await import(new URL('./static/models.js', import.meta.url));
const M = globalThis.LobiqModels;
const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);

// Kyle: lambda * beta = 1/2 whatever the variances; the insider earns sigmaV sigmaU / 2; doubling
// the noise halves price impact.
for (const [sv, su] of [[1, 1], [2, 0.5], [0.3, 7]]) {
  const k = M.kyle(sv, su);
  near(k.lambda * k.beta, 0.5);
  near(k.insiderProfit, sv * su / 2);
  near(M.kyle(sv, 2 * su).lambda, k.lambda / 2);
}

// Glosten-Milgrom: no informed traders, no spread; all informed, quotes at the extremes; the spread
// grows with the informed share; quotes bracket the expected value.
{
  const none = M.gmQuotes(90, 110, 0.5, 0), all = M.gmQuotes(90, 110, 0.5, 1);
  near(none.spread, 0);
  near(all.ask, 110), near(all.bid, 90);
  let last = -1;
  for (const mu of [0.1, 0.3, 0.5, 0.7, 0.9]) {
    const q = M.gmQuotes(90, 110, 0.4, mu);
    assert.ok(q.spread > last), last = q.spread;
    assert.ok(q.bid < 90 + 20 * 0.4 && 90 + 20 * 0.4 < q.ask);
  }
  const path = M.gmPath(90, 110, 0.5, 0.3, 400);
  assert.ok(path.at(-1).delta > 0.99, 'prices learn that V is high');
  assert.ok(path.at(-1).spread < path[0].spread, 'the spread narrows as uncertainty resolves');
}

// Almgren-Chriss: holdings run from X to 0; without risk aversion the schedule is a straight line;
// more risk aversion trades faster: higher expected cost, lower risk.
{
  const twap = M.almgrenChriss(1e6, 1, 10, 0.95, 2.5e-6, 2.5e-7, 0);
  near(twap.holdings[0], 1e6), near(twap.holdings[10], 0, 1e-6), near(twap.holdings[5], 5e5, 1e-6);
  const f = M.acFrontier(1e6, 1, 10, 0.95, 2.5e-6, 2.5e-7, [1e-8, 1e-7, 1e-6, 1e-5]);
  for (let i = 1; i < f.length; ++i) {
    assert.ok(f[i].cost > f[i - 1].cost, 'cost rises with risk aversion');
    assert.ok(f[i].variance < f[i - 1].variance, 'risk falls with risk aversion');
    assert.ok(f[i].holdings[1] < f[i - 1].holdings[1], 'more is sold up front');
  }
}

// Avellaneda-Stoikov: flat inventory quotes symmetrically around the mid; a long position lowers
// both quotes; the spread shrinks toward the end of the horizon.
{
  const flat = M.avellanedaStoikov(100, 0, 0.1, 2, 1.5, 1, 0);
  near((flat.bid + flat.ask) / 2, 100);
  const long = M.avellanedaStoikov(100, 5, 0.1, 2, 1.5, 1, 0);
  assert.ok(long.ask < flat.ask && long.bid < flat.bid);
  assert.ok(M.avellanedaStoikov(100, 0, 0.1, 2, 1.5, 1, 0.9).spread < flat.spread);
}

console.log('models ok');
