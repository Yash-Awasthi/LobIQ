// Every chart viz.js draws for the tools' Markdown, on small samples of each section. Run: node studio/test_viz.mjs
import assert from 'node:assert/strict';

globalThis.window = globalThis;
await import(new URL('./static/viz.js', import.meta.url));
const render = globalThis.vizRender;
const svgs = html => (html.match(/<svg/g) || []).length;

const sections = {
  vol: `## Forecasts on the first test day: QRSIM\n\n| Time | realized | EWMA | HAR | GARCH-t |\n|---|---|---|---|---|\n` +
    Array.from({length: 20}, (_, i) => `| 10:${String(i).padStart(2, '0')} | ${1 + i % 5} | 3 | 2.5 | 2.8 |`).join('\n'),
  hawkes: `## Who triggers whom\n\n| triggered \ by | MO buy | MO sell | LO bid |\n|---|---|---|---|\n| MO buy | 0.2 | 0.01 | 0 |\n| MO sell | 0.02 | 0.3 | 0 |\n| LO bid | 0.04 | 0 | 0.1 |`,
  regime: `## Regimes on the test days\n\n| State | share of minutes | next-minute RV (ticks²) |\n|---|---|---|\n| 0 | 40.0% | 2.1 |\n| 1 | 60.0% | 5.3 |\n\n` +
    `## Transitions on the test days\n\n| from \ to | 0 | 1 |\n|---|---|---|\n| 0 | 0.9 | 0.1 |\n| 1 | 0.2 | 0.8 |`,
  surface: `# Implied volatility surface\n\n| Expiry (y) | forward | ATM vol | ATM skew dvol/dk |\n|---|---|---|---|\n| 0.25 | 100 | 0.20 | -0.21 |\n| 1.0 | 103 | 0.22 | -0.20 |\n\n` +
    `## Implied volatility by log-moneyness\n\n| k | T=0.250 | T=1.000 |\n|---|---|---|\n| -0.10 | 0.23 | 0.24 |\n| +0.00 | 0.20 | 0.22 |\n| +0.10 | 0.19 | 0.21 |`,
  alpha: `## Ridge coefficients\n\n| Feature | QRSIM |\n|---|---|\n| imbalance_l1 | +0.18 |\n| ofi_l2 | -0.03 |\n\n## Symbolic regression\n\n| Symbol | nodes | formula |\n|---|---|---|\n| QRSIM | 3 | \`x\` |\n\n` +
    `Front for QRSIM:\n\n| nodes | fit R² | validation R² |\n|---|---|---|\n| 1 | 0.05 | 0.04 |\n| 3 | 0.06 | 0.05 |`,
  price: `# put S=100 K=105 T=1 r=0.05 q=0 sigma=0.25\n\n| Model | price |\n|---|---|\n| Black-Scholes | 9.88 |`,
};
const want = {vol: 1, hawkes: 1, regime: 2, surface: 3, alpha: 2, price: 2};
for (const [name, md] of Object.entries(sections)) {
  const html = render(md);
  assert.equal(svgs(html), want[name], `${name}: ${svgs(html)} charts`);
  assert.ok(!/NaN|undefined/.test(html), `${name}: NaN or undefined in the output`);
}

// The network draws an arrow per weight of at least 5% of the largest, loops included (0.01 is left out).
assert.equal((render(sections.hawkes).match(/marker-end=/g) || []).length, 5);
assert.ok(/<table/.test(render(sections.price)) && /<table/.test(render(sections.vol)));  // every table survives its chart
// A plain section is a plain table; text is escaped.
assert.equal(svgs(render('## Other\n\n| a | b |\n|---|---|\n| 1 | <x> |')), 0);
assert.ok(render('| a |\n|---|\n| <script> |').includes('&lt;script&gt;'));
console.log('viz ok');
