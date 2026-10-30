// Tool output (Markdown) to HTML with charts: tables become tables, and the sections the forecasting
// tools print also get the picture that reads them best. Shared by the studio and the published model
// pages (scripts/demo/md2html.py); inline SVG on the page's colour tokens, no libraries.
(() => {
  const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
  const num = s => { const v = parseFloat(String(s).replace(/[,%]/g, '')); return Number.isFinite(v) ? v : NaN; };
  const fmt = (v, d = 2) => !Number.isFinite(v) ? '–' : Math.abs(v) >= 1e4 || (Math.abs(v) < 1e-3 && v !== 0) ? v.toExponential(1) : v.toFixed(d);
  const color = i => `var(--c${(i % 8) + 1})`;
  const inline = s => esc(s).replace(/\*\*(.+?)\*\*/g, '<b>$1</b>').replace(/`(.+?)`/g, '<code>$1</code>');
  let uid = 0;

  // Rounded tick values covering [lo, hi]; integer steps only with whole.
  function ticks(lo, hi, n = 5, whole = false) {
    if (!(hi > lo)) return [lo];
    const raw = (hi - lo) / n, mag = 10 ** Math.floor(Math.log10(raw));
    let step = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => s >= raw);
    if (whole) step = Math.max(1, Math.round(step));
    const out = [];
    for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) out.push(+v.toFixed(12));
    out.step = step;
    return out;
  }
  // As many decimals as the tick step needs, no more.
  const tickFmt = (v, step) => Math.abs(v) >= 1e5 ? v.toExponential(1) : v.toFixed(Math.max(0, Math.min(6, -Math.floor(Math.log10(step || 1) + 1e-9))));

  // series: [{name, pts: [[x, y]], dash, dots, width}]; xfmt formats x tick labels; logY plots log10(y).
  function lineChart({series, xlab = '', ylab = '', xfmt = null, logY = false, W = 900, H = 300, marks = [], wholeX = false}) {
    const T = y => logY ? Math.log10(Math.max(y, 1e-12)) : y;
    const pts = series.flatMap(s => s.pts).filter(p => Number.isFinite(p[0]) && Number.isFinite(p[1]) && (!logY || p[1] > 0));
    if (!pts.length) return '';
    let x0 = Math.min(...pts.map(p => p[0])), x1 = Math.max(...pts.map(p => p[0]));
    let y0 = Math.min(...pts.map(p => T(p[1]))), y1 = Math.max(...pts.map(p => T(p[1])));
    if (y1 === y0) y0 -= 1, y1 += 1;
    const pad = (y1 - y0) * 0.06; y0 -= pad; y1 += pad;
    const L = 64, R = 16, Tp = 30, B = 42, X = v => L + (v - x0) / ((x1 - x0) || 1) * (W - L - R), Y = v => H - B - (T(v) - y0) / (y1 - y0) * (H - Tp - B);
    const yt = logY ? ticks(Math.ceil(y0), Math.floor(y1), 4).map(v => 10 ** v) : ticks(y0, y1);
    const xt = ticks(x0, x1, 6, wholeX);
    xfmt ??= v => tickFmt(v, xt.step);
    const yl = v => logY ? fmt(v, v < 1 ? 3 : v < 10 ? 2 : 0) : tickFmt(v, yt.step);
    let g = yt.map(v => `<line x1="${L}" x2="${W - R}" y1="${Y(v)}" y2="${Y(v)}" class="grid"/><text x="${L - 6}" y="${Y(v) + 4}" class="ax" text-anchor="end">${yl(v)}</text>`).join('');
    g += xt.map(v => `<text x="${X(v)}" y="${H - B + 16}" class="ax" text-anchor="middle">${esc(xfmt(v))}</text>`).join('');
    const body = series.map((s, i) => {
      const c = s.color || color(i), ok = s.pts.filter(p => Number.isFinite(p[1]) && (!logY || p[1] > 0));
      if (s.dots) return ok.map(p => `<circle cx="${X(p[0]).toFixed(1)}" cy="${Y(p[1]).toFixed(1)}" r="${s.r || 2}" fill="${c}" opacity="${s.opacity ?? 0.55}"><title>${esc(s.name)} ${esc(xfmt(p[0]))}: ${fmt(p[1], 4)}</title></circle>`).join('');
      return `<path d="${ok.map((p, k) => `${k ? 'L' : 'M'}${X(p[0]).toFixed(1)},${Y(p[1]).toFixed(1)}`).join('')}" fill="none" stroke="${c}" stroke-width="${s.width || 2}"${s.dash ? ' stroke-dasharray="5 4"' : ''}/>`;
    }).join('');
    const mk = marks.map(m => `<circle cx="${X(m.x)}" cy="${Y(m.y)}" r="6" class="mark"><title>${esc(m.label)}</title></circle><text x="${X(m.x) + 9}" y="${Y(m.y) - 8}" class="ax">${esc(m.label)}</text>`).join('');
    const legend = series.map((s, i) => `<span><i style="background:${s.color || color(i)}${s.dash ? ';opacity:.6' : ''}"></i>${esc(s.name)}</span>`).join('');
    return `<figure class="viz"><svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(ylab)} by ${esc(xlab)}">${g}
      <line x1="${L}" x2="${W - R}" y1="${H - B}" y2="${H - B}" class="axis"/>${body}${mk}
      <text x="${(L + W - R) / 2}" y="${H - 6}" class="ax" text-anchor="middle">${esc(xlab)}</text>
      <text x="14" y="${(Tp + H - B) / 2}" class="ax" text-anchor="middle" transform="rotate(-90 14 ${(Tp + H - B) / 2})">${esc(ylab)}${logY ? ' (log scale)' : ''}</text></svg>
      <figcaption class="vlegend">${legend}</figcaption></figure>`;
  }

  // Horizontal bars from zero: positive one colour, negative the other.
  function bars(items, {title = '', W = 900} = {}) {
    if (!items.length) return '';
    const rowH = 22, H = items.length * rowH + 34, L = 190, R = 70, max = Math.max(...items.map(d => Math.abs(d.v)), 1e-12);
    const anyNeg = items.some(d => d.v < 0), zx = anyNeg ? L + (W - L - R) / 2 : L, half = anyNeg ? (W - L - R) / 2 : W - L - R;
    const rows = items.map((d, i) => {
      const w = Math.abs(d.v) / max * half, x = d.v < 0 ? zx - w : zx, y = 18 + i * rowH;
      return `<text x="${L - 8}" y="${y + 14}" class="ax" text-anchor="end">${esc(d.label)}</text>
        <rect x="${x.toFixed(1)}" y="${y + 3}" width="${Math.max(w, 1).toFixed(1)}" height="${rowH - 7}" rx="2" fill="${d.v < 0 ? 'var(--neg-fill)' : 'var(--c1)'}" opacity=".8"><title>${esc(d.label)}: ${fmt(d.v, 4)}</title></rect>
        <text x="${d.v < 0 ? x - 4 : x + w + 4}" y="${y + 14}" class="ax" text-anchor="${d.v < 0 ? 'end' : 'start'}">${fmt(d.v, 3)}</text>`;
    }).join('');
    return `<figure class="viz"><svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(title)}">
      <line x1="${zx}" x2="${zx}" y1="10" y2="${H - 10}" class="axis"/>${rows}</svg>${title ? `<figcaption class="muted">${esc(title)}</figcaption>` : ''}</figure>`;
  }

  // Directed graph on a circle: edges from -> to with width by weight, self loops above their node.
  function network(nodes, edges, {W = 640, H = 460, caption = ''} = {}) {
    const id = `vz${++uid}`, cx = W / 2, cy = H / 2 + 6, n = nodes.length, R = n === 2 ? W / 2 - 120 : Math.min(W, H) / 2 - 70;
    const start = n === 2 ? Math.PI : -Math.PI / 2;  // two nodes side by side, more around a circle from the top
    const pos = nodes.map((_, i) => ({x: cx + R * Math.cos(start + 2 * Math.PI * i / n), y: cy + R * Math.sin(start + 2 * Math.PI * i / n)}));
    const wmax = Math.max(...edges.map(e => e.w), 1e-12), maxSize = Math.max(...nodes.map(d => d.size ?? 1), 1e-12);
    const rad = i => 16 + 18 * Math.sqrt((nodes[i].size ?? 1) / maxSize);
    const arcs = edges.filter(e => e.w > 0).sort((a, b) => a.w - b.w).map(e => {
      const sw = (0.8 + 7 * e.w / wmax).toFixed(1), op = (0.25 + 0.6 * e.w / wmax).toFixed(2), tip = `<title>${esc(e.title)}</title>`;
      const p = pos[e.from], q = pos[e.to], ra = rad(e.from), rb = rad(e.to);
      if (e.from === e.to) {  // loop on the outside of the circle
        const ox = (p.x - cx) / R, oy = (p.y - cy) / R, bx = p.x + ox * (ra + 26), by = p.y + oy * (ra + 26), nx = -oy * 16, ny = ox * 16;
        return `<path d="M${p.x + ox * ra + nx * 0.5},${p.y + oy * ra + ny * 0.5} C${bx + nx},${by + ny} ${bx - nx},${by - ny} ${p.x + ox * ra - nx * 0.5},${p.y + oy * ra - ny * 0.5}"
          fill="none" stroke="var(--c2)" stroke-width="${sw}" opacity="${op}" marker-end="url(#${id})">${tip}</path>`;
      }
      const dx = q.x - p.x, dy = q.y - p.y, d = Math.hypot(dx, dy) || 1, ux = dx / d, uy = dy / d;
      const mx = (p.x + q.x) / 2 - uy * 26, my = (p.y + q.y) / 2 + ux * 26;  // each direction bends to its own side
      return `<path d="M${p.x + ux * ra},${p.y + uy * ra} Q${mx},${my} ${q.x - ux * (rb + 6)},${q.y - uy * (rb + 6)}"
        fill="none" stroke="var(--c1)" stroke-width="${sw}" opacity="${op}" marker-end="url(#${id})">${tip}</path>`;
    }).join('');
    const dots = nodes.map((d, i) => `<circle cx="${pos[i].x}" cy="${pos[i].y}" r="${rad(i)}" fill="${d.color || color(i)}" class="node"><title>${esc(d.title || d.label)}</title></circle>
      <text x="${pos[i].x}" y="${pos[i].y + rad(i) + 15}" class="ax" text-anchor="middle">${esc(d.label)}</text>`).join('');
    return `<figure class="viz"><svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(caption)}" style="max-width:${W}px;margin:0 auto">
      <defs><marker id="${id}" viewBox="0 0 10 10" refX="8" refY="5" markerUnits="userSpaceOnUse" markerWidth="11" markerHeight="11" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" class="arrowhead"/></marker></defs>${arcs}${dots}</svg>${caption ? `<figcaption class="muted">${esc(caption)}</figcaption>` : ''}</figure>`;
  }

  // A table whose numeric cells are shaded by value (sequential from zero, or diverging around it).
  function heatTable(t, {diverging = false, firstCol = 1} = {}) {
    const vals = t.body.flatMap(r => r.slice(firstCol).map(num)).filter(Number.isFinite);
    const lo = Math.min(...vals), hi = Math.max(...vals), m = Math.max(Math.abs(lo), Math.abs(hi)) || 1;
    const shade = v => {
      if (!Number.isFinite(v)) return '';
      const a = diverging ? Math.abs(v) / m : 0.08 + 0.92 * (v - lo) / ((hi - lo) || 1);  // light at the minimum, dark at the maximum
      return `background:color-mix(in srgb, ${diverging && v < 0 ? 'var(--neg-fill)' : 'var(--c1)'} ${Math.round(a * 70)}%, transparent)`;
    };
    return `<div class="scroll"><table class="heatmap"><thead><tr>${t.head.map(c => `<th>${inline(c)}</th>`).join('')}</tr></thead><tbody>${t.body.map(r =>
      `<tr>${r.map((c, j) => j < firstCol ? `<th>${inline(c)}</th>` : `<td class="heat" style="${shade(num(c))}">${inline(c)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
  }

  const plainTable = t => `<div class="scroll"><table><thead><tr>${t.head.map(c => `<th>${inline(c)}</th>`).join('')}</tr></thead><tbody>${t.body.map(r =>
    `<tr>${r.map(c => `<td>${inline(c)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
  const col = (t, name) => t.head.findIndex(h => h.toLowerCase() === name.toLowerCase());

  // Black-Scholes on the client for the option price chart.
  const ncdf = x => { const t = 1 / (1 + 0.2316419 * Math.abs(x)), d = 0.3989423 * Math.exp(-x * x / 2);
    const p = d * t * (0.3193815 + t * (-0.3565638 + t * (1.781478 + t * (-1.821256 + t * 1.330274)))); return x > 0 ? 1 - p : p; };
  function bs(call, S, K, T, r, q, v) {
    const s = v * Math.sqrt(T), d1 = (Math.log(S / K) + (r - q + v * v / 2) * T) / s, d2 = d1 - s;
    return call ? {p: S * Math.exp(-q * T) * ncdf(d1) - K * Math.exp(-r * T) * ncdf(d2), d: Math.exp(-q * T) * ncdf(d1)}
                : {p: K * Math.exp(-r * T) * ncdf(-d2) - S * Math.exp(-q * T) * ncdf(-d1), d: -Math.exp(-q * T) * ncdf(-d1)};
  }

  // Section heading -> picture drawn before (or instead of) its table. ctx holds every table by heading.
  const hooks = [
    [/^Forecasts on the first test day/, t => {
      const tc = col(t, 'Time'), x = t.body.map((_, i) => i), at = i => t.body[Math.round(i)]?.[tc] ?? '';
      const s = (name, j, o = {}) => ({name, pts: t.body.map((r, i) => [x[i], num(r[j])]), ...o});
      return {before: lineChart({series: [s('realized', col(t, 'realized'), {dots: true, color: 'var(--muted)'}), s('EWMA', col(t, 'EWMA'), {color: 'var(--c2)'}),
        s('HAR', col(t, 'HAR'), {color: 'var(--c3)'}), s('GARCH-t', col(t, 'GARCH-t'), {color: 'var(--c1)'})], xlab: 'time (ET)', ylab: 'variance per minute (ticks²)', xfmt: at, logY: true}),
        collapse: true};
    }],
    [/^Who triggers whom/, t => {
      const names = t.head.slice(1), nodes = names.map((n, i) => ({label: n, color: /bid|sell/.test(n) ? 'var(--bid)' : 'var(--ask)', title: n})), edges = [];
      t.body.forEach((r, i) => r.slice(1).forEach((c, j) => { const w = num(c); if (w > 0) edges.push({from: j, to: i, w, title: `${names[j]} → ${r[0]}: ${fmt(w, 3)} events triggered per event`}); }));
      const top = Math.max(...edges.map(e => e.w), 0);
      return {before: network(nodes, edges.filter(e => e.w >= top * 0.05), {caption: 'Arrows: from the triggering event type to the type it triggers, wider for more events triggered per event; loops: self-excitation. Bid side and market sells in one colour, ask side and market buys in the other.'}),
        replace: heatTable(t)};
    }],
    [/^Transitions on the test days/, (t, ctx) => {
      const reg = ctx['Regimes on the test days'], share = reg ? reg.body.map(r => num(r[col(reg, 'share of minutes')])) : [];
      const rv = reg ? reg.body.map(r => num(r[col(reg, 'next-minute RV (ticks²)')])) : [];
      const nodes = t.body.map((r, i) => ({label: `state ${r[0]}`, size: share[i] || 1, title: `state ${r[0]}: ${fmt(share[i], 1)}% of minutes, next-minute RV ${fmt(rv[i], 3)}`}));
      const edges = [];
      t.body.forEach((r, i) => r.slice(1).forEach((c, j) => { const w = num(c); if (w >= 0.02) edges.push({from: i, to: j, w, title: `state ${i} → state ${j}: ${fmt(w * 100, 1)}%`}); }));
      return {before: network(nodes, edges, {W: 560, H: nodes.length === 2 ? 230 : 420, caption: 'Circles: regimes, sized by their share of test minutes (state 0 the calmest). Arrows: where the next minute went, wider for more often; loops: staying.'}), replace: heatTable(t)};
    }],
    [/^Regimes on the test days/, t => ({before: bars(t.body.map(r => ({label: `state ${r[0]} (${r[col(t, 'share of minutes')]})`, v: num(r[col(t, 'next-minute RV (ticks²)')])})),
      {title: 'Next-minute realized variance by regime: a regime that predicts calm or storm is worth acting on.'})})],
    [/^Implied volatility surface/, t => {
      const T = t.body.map(r => num(r[0])), atm = t.body.map(r => num(r[col(t, 'ATM vol')])), skew = t.body.map(r => num(r[col(t, 'ATM skew dvol/dk')]));
      return {before: `<div class="grid2 even">${lineChart({series: [{name: 'ATM implied volatility', pts: T.map((x, i) => [x, atm[i]])}], xlab: 'expiry (years)', ylab: 'ATM vol', W: 560, H: 250})}${
        lineChart({series: [{name: 'ATM skew dvol/dk', pts: T.map((x, i) => [x, skew[i]]), color: 'var(--c2)'}], xlab: 'expiry (years)', ylab: 'skew', W: 560, H: 250})}</div>`};
    }],
    [/^Implied volatility by log-moneyness/, t => {
      const ks = t.body.map(r => num(r[0]));
      return {before: lineChart({series: t.head.slice(1).map((h, j) => ({name: h, pts: t.body.map((r, i) => [ks[i], num(r[j + 1])])})), xlab: 'log-moneyness k = log(K/F)', ylab: 'implied volatility'}),
        replace: heatTable(t)};
    }],
    [/^Ridge coefficients/, t => ({before: bars(t.body.map(r => ({label: r[0], v: num(r[1])})).sort((a, b) => Math.abs(b.v) - Math.abs(a.v)),
      {title: `${t.head[1]}: predicted mid change (ticks) per standard deviation of each feature, largest first.`}), collapse: true})],
    [/^Front for /, (t, ctx, heading) => {
      const sym = heading.replace(/^Front for /, '').replace(/:$/, ''), sr = ctx['Symbolic regression'];
      const chosen = sr?.body.find(r => r[0] === sym), n = col(t, 'nodes');
      const val = t.body.map(r => [num(r[n]), num(r[col(t, 'validation R²')])]), fit = t.body.map(r => [num(r[n]), num(r[col(t, 'fit R²')])]);
      const pick = chosen && val.find(p => p[0] === num(chosen[1]));
      return {before: lineChart({series: [{name: 'fit R² (search data)', pts: fit, color: 'var(--muted)', dash: true}, {name: 'validation R² (held-out train days)', pts: val, color: 'var(--c1)'}],
        xlab: 'formula size (nodes)', ylab: 'R²', wholeX: true, H: 240, marks: pick ? [{x: pick[0], y: pick[1], label: 'chosen'}] : []})};
    }],
    [/^(call|put) S=/, (t, ctx, heading) => {
      const kv = Object.fromEntries([...heading.matchAll(/(\w+)=([-\d.eE]+)/g)].map(m => [m[1], +m[2]])), call = heading.startsWith('call');
      const xs = Array.from({length: 121}, (_, i) => kv.K * (0.5 + i / 120)), val = xs.map(S => bs(call, S, kv.K, kv.T, kv.r, kv.q, kv.sigma));
      return {before: `<div class="grid2 even">${lineChart({series: [{name: 'Black-Scholes price', pts: xs.map((S, i) => [S, val[i].p])},
        {name: 'payoff at expiry', pts: xs.map(S => [S, Math.max(call ? S - kv.K : kv.K - S, 0)]), color: 'var(--muted)', dash: true}], xlab: 'spot', ylab: 'option value', W: 560, H: 280,
        marks: [{x: kv.S, y: bs(call, kv.S, kv.K, kv.T, kv.r, kv.q, kv.sigma).p, label: 'now'}]})}${lineChart({series: [{name: 'delta', pts: xs.map((S, i) => [S, val[i].d]), color: 'var(--c3)'}],
        xlab: 'spot', ylab: 'delta', W: 560, H: 280, marks: [{x: kv.S, y: bs(call, kv.S, kv.K, kv.T, kv.r, kv.q, kv.sigma).d, label: 'now'}]})}</div>`};
    }],
  ];

  function render(src) {
    const lines = src.split(/\r?\n/), blocks = [];
    for (let i = 0; i < lines.length; i++) {
      const l = lines[i];
      if (/^\|/.test(l)) {
        const rows = [];
        for (; i < lines.length && /^\|/.test(lines[i]); i++) if (!/^\|[-| :]+\|$/.test(lines[i])) rows.push(lines[i].slice(1, -1).split('|').map(c => c.trim()));
        i--;
        blocks.push({type: 'table', t: {head: rows[0], body: rows.slice(1)}});
      } else if (/^ {4}\S/.test(l)) blocks.push({type: 'pre', text: l.slice(4)});
      else if (/^#{1,4} /.test(l)) blocks.push({type: 'h', text: l.replace(/^#+ /, '')});
      else if (/^- /.test(l)) blocks.push({type: 'li', text: l.slice(2)});
      else if (l.trim()) blocks.push({type: 'p', text: l});
    }
    // Tables by the heading (or "Front for X:" line) above them, for hooks that combine sections.
    const ctx = {};
    let heading = '';
    for (const b of blocks) {
      if (b.type === 'h' || (b.type === 'p' && /^Front for /.test(b.text))) heading = b.text;
      if (b.type === 'table' && !(heading in ctx)) ctx[heading] = b.t;
    }
    heading = '';
    return blocks.map(b => {
      if (b.type === 'h') { heading = b.text; return `<h3>${inline(b.text)}</h3>`; }
      if (b.type === 'p' && /^Front for /.test(b.text)) { heading = b.text; return `<p>${inline(b.text)}</p>`; }
      if (b.type === 'table') {
        const hook = hooks.find(([re]) => re.test(heading));
        let out = null;
        try { out = hook && hook[1](b.t, ctx, heading); } catch (e) { out = null; }  // a chart never costs the table
        const table = out?.replace ?? plainTable(b.t);
        const long = (out?.collapse && b.t.body.length > 12) || b.t.body.length > 40;
        const body = long ? `<details><summary>Table (${b.t.body.length} rows)</summary>${table}</details>` : table;
        return (out?.before ?? '') + body;
      }
      if (b.type === 'pre') return `<pre class="log">${esc(b.text)}</pre>`;
      if (b.type === 'li') return `<li>${inline(b.text)}</li>`;
      return `<p>${inline(b.text)}</p>`;
    }).join('');
  }

  window.vizRender = render;
})();
