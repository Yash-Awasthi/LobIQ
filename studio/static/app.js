'use strict';
const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const fmt = (x, d = 2) => x == null || Number.isNaN(x) ? '–' : Number(x).toLocaleString(undefined, {minimumFractionDigits: d, maximumFractionDigits: d});
const bytes = n => n > 1e9 ? fmt(n / 1e9, 1) + ' GB' : n > 1e6 ? fmt(n / 1e6, 1) + ' MB' : fmt(n / 1e3, 0) + ' kB';
const lines = s => s.split('\n').map(x => x.trim()).filter(Boolean);
const sign = x => x == null ? '' : x > 0 ? 'pos' : x < 0 ? 'neg' : '';

// A snapshot (hft studio --snapshot) is static files: reads come from snapshot.json, writes are refused.
const SNAP = !!document.querySelector('meta[name=hft-snapshot]');
let SNAPDATA = null;
async function api(path, body) {
  if (SNAP) {
    SNAPDATA ??= await (await fetch('snapshot.json')).json();
    const found = !body && ({'/api/state': SNAPDATA.state, '/api/leaderboard': SNAPDATA.leaderboard, '/api/strategies': SNAPDATA.strategies}[path]
      ?? (path.startsWith('/api/report?path=') && SNAPDATA.leaderboard.find(r => r.path === decodeURIComponent(path.slice(17)))));
    if (found) return found;
    throw new Error('Read-only snapshot: run hft studio locally for this');
  }
  const r = await fetch(path, body ? {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)} : {});
  const j = await r.json().catch(() => ({error: r.statusText}));
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}

function toast(msg, bad) {
  const t = document.createElement('div');
  t.className = 'toast' + (bad ? ' bad' : '');
  t.textContent = msg;
  document.body.append(t);
  setTimeout(() => t.remove(), 5000);
}

// Tabs, remembered in the URL hash.
function show(tab) {
  $$('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === tab));
  $$('.tab').forEach(s => s.classList.toggle('on', s.id === tab));
  if (tab === 'leaderboard') loadLeaderboard();
  if (tab === 'live' && STATE?.jobs.some(j => j.running && j.spec.kind === 'live')) $('#dash').src = 'http://127.0.0.1:8088/';
  history.replaceState(null, '', '#' + tab);
}
$$('#tabs button').forEach(b => b.onclick = () => show(b.dataset.tab));

// A job: start it, stream its output into a <pre>, resolve with its final view.
function runJob(spec, logEl, statusEl) {
  return api('/api/jobs', spec).then(job => new Promise(resolve => {
    if (logEl) logEl.textContent = '$ hft ' + job.args.join(' ') + '\n';
    if (statusEl) statusEl.textContent = 'Running job #' + job.id + '…';
    const es = new EventSource('/api/jobs/' + job.id + '/stream');
    es.addEventListener('line', e => {
      if (!logEl) return;
      logEl.textContent += JSON.parse(e.data) + '\n';
      logEl.scrollTop = logEl.scrollHeight;
    });
    es.addEventListener('done', e => {
      es.close();
      const v = JSON.parse(e.data);
      if (statusEl) statusEl.textContent = `Job #${v.id} ${v.rc === 0 ? 'finished' : 'failed (exit ' + v.rc + ')'} in ${fmt(v.ended - v.started, 1)} s.`;
      refresh();
      api('/api/jobs/' + v.id).then(resolve);
    });
    refresh();
  })).catch(e => { toast(e.message, true); if (statusEl) statusEl.textContent = e.message; });
}

// Overview, days, runs, jobs.
let STATE = null;
async function refresh() {
  STATE = await api('/api/state');
  const s = STATE;
  const warn = $('#warn');
  warn.hidden = !!s.bin && !SNAP;
  warn.innerHTML = SNAP ? 'Read-only snapshot of the demo reports, built by CI. Run <code>hft studio</code> locally to run backtests, scrub order books and trade on paper.'
    : 'Tools not built: run <code>cmake --preset release &amp;&amp; cmake --build build/release</code> or set <code>HFT_BIN</code>. Browsing existing reports still works.';
  const running = s.jobs.filter(j => j.running).length;
  $('#running').hidden = !running;
  $('#running').textContent = running;
  const best = s.runs.map(r => parseFloat(r.summary.ledger_pnl_usd)).filter(x => !Number.isNaN(x));
  $('#cards').innerHTML = [
    ['Nasdaq days', s.days.length], ['Symbol-days', s.days.reduce((a, d) => a + d.symbols, 0).toLocaleString()],
    ['Live paper runs', s.runs.length], ['Paper PnL, all runs', best.length ? fmt(best.reduce((a, b) => a + b, 0)) + ' USD' : '–'],
    ['Jobs running', running], ['Data', s.data],
  ].map(([k, v]) => `<div class="card"><div class="k">${esc(k)}</div><div class="v">${esc(v)}</div></div>`).join('');
  $('#days tbody').innerHTML = s.days.map(d => `<tr><td>${d.day}</td><td>${d.date}</td><td class="n">${d.symbols.toLocaleString()}</td>
    <td class="n">${bytes(d.bytes)}</td><td><button class="link" data-replay="${d.day}">replay</button></td></tr>`).join('')
    || '<tr><td colspan="5" class="muted">No stores yet. Download a day or run the demos.</td></tr>';
  $$('[data-replay]').forEach(b => b.onclick = () => { show('analysis'); $('#an-form [name=kind]').value = 'replay'; syncAnalysis(); $('#an-form [name=day]').value = b.dataset.replay; $('#an-form').requestSubmit(); });
  const opts = s.days.map(d => `<option value="${d.day}">${d.date} (${d.day}, ${d.symbols} symbols)</option>`).join('');
  $$('select[name=day]').forEach(sel => { const v = sel.value; sel.innerHTML = opts; if (v) sel.value = v; });
  $('#runs tbody').innerHTML = s.runs.map(r => `<tr><td>${esc(r.run)}</td><td>${esc(r.status)}</td>
    <td class="n ${sign(parseFloat(r.summary.ledger_pnl_usd))}">${esc(r.summary.ledger_pnl_usd ?? '–')}</td><td class="n">${esc(r.summary.orders ?? '–')}</td>
    <td>${r.report ? `<a href="${r.report}" target="_blank">report</a>` : `<button class="link" data-review="${esc(r.run)}">review</button>`}
    ${r.status === 'running or stopped' ? `<button class="link danger" data-kill="${esc(r.run)}">kill</button>` : ''}</td></tr>`).join('')
    || '<tr><td colspan="5" class="muted">No live runs yet.</td></tr>';
  $$('[data-review]').forEach(b => b.onclick = () => runJob({kind: 'review', run: b.dataset.review}, $('#job-log')).then(() => show('jobs')));
  $$('[data-kill]').forEach(b => b.onclick = () => confirm('Trip the kill switch for run ' + b.dataset.kill + '? Every paper order is cancelled.') &&
    api('/api/kill', {run: b.dataset.kill}).then(() => toast('Kill switch set'), e => toast(e.message, true)));
  const row = j => `<tr><td>${j.id}</td><td><code>hft ${esc(j.args.join(' '))}</code></td>
    <td class="${j.running ? '' : j.rc === 0 ? 'pos' : 'neg'}">${j.running ? 'running' : j.rc === 0 ? 'ok' : 'exit ' + j.rc}</td>
    <td class="n">${fmt(((j.ended || Date.now() / 1000) - j.started), 1)} s</td>
    <td>${j.report ? `<a href="${j.report}" target="_blank">report</a> ` : ''}<button class="link" data-log="${j.id}">log</button>
    ${j.running ? `<button class="link danger" data-cancel="${j.id}">stop</button>` : ''}</td></tr>`;
  $('#jobtable tbody').innerHTML = s.jobs.map(row).join('') || '<tr><td colspan="5" class="muted">No jobs yet.</td></tr>';
  $('#recent').innerHTML = s.jobs.slice(0, 6).map(j => `<li><code>hft ${esc(j.args.join(' '))}</code> <span class="muted">${j.running ? 'running' : j.rc === 0 ? 'ok' : 'failed'}</span></li>`).join('') || '<li class="muted">None.</li>';
  $$('[data-log]').forEach(b => b.onclick = async () => { show('jobs'); $('#job-log').textContent = (await api('/api/jobs/' + b.dataset.log)).lines.join('\n'); });
  $$('[data-cancel]').forEach(b => b.onclick = () => api(`/api/jobs/${b.dataset.cancel}/cancel`, {}).then(refresh));
}

$('#get-form').onsubmit = e => { e.preventDefault(); runJob({kind: 'get', days: e.target.days.value.trim()}, $('#job-log')); show('jobs'); };
$('#demo').onclick = () => { runJob({kind: 'demo'}, $('#job-log')).then(() => toast('Demos finished: see the leaderboard')); show('jobs'); };

// Backtest.
async function loadStrategies() {
  const list = await api('/api/strategies').catch(() => []);
  const sel = $('#bt-form [name=strategy]');
  sel.innerHTML = list.map(s => `<option value="${esc(s.name.replace(':<arg>', ':'))}" data-help="${esc(s.help)}">${esc(s.name)}</option>`).join('');
  sel.onchange = () => $('#strat-help').textContent = sel.selectedOptions[0]?.dataset.help || '';
  sel.value = list.some(s => s.name === 'avellaneda_stoikov') ? 'avellaneda_stoikov' : sel.value;
  sel.onchange();
}

$('#bt-form').onsubmit = async e => {
  e.preventDefault();
  const f = e.target;
  let strategy = f.strategy.value;
  if (strategy.endsWith(':')) strategy += prompt('Argument for ' + strategy) ?? '';
  const spec = {kind: 'backtest', day: f.day.value, symbols: f.symbols.value.trim().toUpperCase(), strategy,
    params: lines(f.params.value), sweeps: lines(f.sweeps.value)};
  $('#bt-result').innerHTML = '';
  const job = await runJob(spec, $('#bt-log'), $('#bt-status'));
  if (!job?.report) return;
  const lb = await api('/api/leaderboard');
  const mine = lb.filter(r => r.group === job.report || r.path === job.report);
  $('#bt-result').innerHTML = `<p><a href="${job.report}" target="_blank">Open HTML report</a></p>` + table(mine.sort((a, b) => b.dsr - a.dsr));
  bindRows($('#bt-result'), mine);
};

// Leaderboard.
const COLS = [
  ['strategy', 'Strategy', r => esc(r.strategy ?? '?')], ['symbol', 'Symbol', r => esc(r.symbol ?? '?')],
  ['label', 'Run', r => esc(r.label)],
  ['total', 'PnL USD', r => `<span class="${sign(+r.summary.total_usd)}">${fmt(+r.summary.total_usd)}</span>`, r => +r.summary.total_usd],
  ['sharpe_annual', 'Sharpe (ann.)', r => fmt(r.sharpe_annual)], ['psr', 'PSR', r => fmt(r.psr, 3)],
  ['dsr', 'DSR', r => `<span class="${r.dsr > 0.95 ? 'pos' : r.dsr < 0.5 ? 'neg' : ''}">${fmt(r.dsr, 3)}</span>`],
  ['trials', 'Trials', r => r.trials],
  ['max_drawdown_usd', 'Max DD USD', r => fmt(r.max_drawdown_usd)], ['hit_rate', 'Hit rate', r => fmt(r.hit_rate * 100, 1) + '%', r => r.hit_rate],
  ['fills', 'Fills', r => (+r.summary.fills).toLocaleString(), r => +r.summary.fills],
  ['maker_share', 'Maker %', r => fmt(r.maker_share * 100, 0), r => r.maker_share],
  ['fees', 'Fees net USD', r => fmt(+r.summary.fees_usd), r => +r.summary.fees_usd],
];
let LB = [], SORT = ['dsr', -1], SHOWN = [];
const SEL = new Set();
function table(rows, pick) {
  if (!rows.length) return '<p class="muted">No reports yet. Run a backtest or the demos.</p>';
  return `<table class="lb"><thead><tr>${pick ? '<th></th>' : ''}${COLS.map(([k, h]) => `<th data-k="${k}" class="${k === SORT[0] ? 'sorted' : ''}">${h}</th>`).join('')}</tr></thead>
    <tbody>${rows.map((r, i) => `<tr data-i="${i}" class="click">${pick ? `<td><input type="checkbox" data-sel="${esc(r.path)}" ${SEL.has(r.path) ? 'checked' : ''} aria-label="select for comparison"></td>` : ''}${COLS.map(([, , f]) => `<td>${f(r)}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
}
function bindRows(el, rows) { $$('tr[data-i]', el).forEach(tr => tr.onclick = () => detail(rows[+tr.dataset.i])); }
async function loadLeaderboard() {
  LB = await api('/api/leaderboard');
  drawLeaderboard();
}
function drawLeaderboard() {
  let rows = LB;
  if ($('#lb-best').checked) {
    const best = {};
    for (const r of rows) if (!best[r.group] || +r.summary.total_usd > +best[r.group].summary.total_usd) best[r.group] = r;
    rows = Object.values(best);
  }
  const col = COLS.find(c => c[0] === SORT[0]);
  const key = col[3] || (r => r[SORT[0]]);
  rows = [...rows].sort((a, b) => ((key(b) ?? -Infinity) > (key(a) ?? -Infinity) ? 1 : -1) * -SORT[1]);
  const wrap = $('#lb-wrap');
  SHOWN = rows;
  wrap.innerHTML = table(rows, true);
  $$('[data-sel]', wrap).forEach(c => { c.onclick = e => e.stopPropagation(); c.onchange = () => c.checked ? SEL.add(c.dataset.sel) : SEL.delete(c.dataset.sel); });
  $$('th[data-k]', wrap).forEach(th => th.onclick = () => { SORT = [th.dataset.k, SORT[0] === th.dataset.k ? -SORT[1] : -1]; drawLeaderboard(); });
  bindRows(wrap, rows);
}
$('#lb-best').onchange = drawLeaderboard;

const PALETTE = ['#2563eb', '#dc2626', '#059669', '#d97706', '#7c3aed', '#0891b2', '#db2777', '#65a30d'];
$('#lb-compare').onclick = () => {
  const runs = LB.filter(r => SEL.has(r.path) && r.curve?.length).slice(0, PALETTE.length);
  const el = $('#compare');
  if (runs.length < 2) return toast('Tick at least two runs to compare');
  const W = 760, H = 260, P = 40, all = runs.flatMap(r => r.curve).concat(0);
  const lo = Math.min(...all), hi = Math.max(...all), span = hi - lo || 1, n = Math.max(...runs.map(r => r.curve.length));
  const x = i => P + i * (W - 2 * P) / n, y = v => H - P - (v - lo) / span * (H - 2 * P);
  const paths = runs.map((r, k) => `<path d="${[0, ...r.curve].map((v, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join('')}" fill="none" stroke="${PALETTE[k]}" stroke-width="1.6"/>`).join('');
  el.hidden = false;
  el.innerHTML = `<h2>Cumulative PnL by minute</h2><svg viewBox="0 0 ${W} ${H}" role="img" aria-label="PnL curves compared">
    <line x1="${P}" x2="${W - P}" y1="${y(0)}" y2="${y(0)}" class="zero"/>${paths}
    <text x="${P}" y="14" class="ax">${fmt(hi)} USD</text><text x="${P}" y="${H - 8}" class="ax">${fmt(lo)} USD</text>
    <text x="${W - P}" y="${H - 8}" class="ax" text-anchor="end">${n} min</text></svg>
    <ul class="plain legend">${runs.map((r, k) => `<li><i style="background:${PALETTE[k]}"></i>${esc(r.strategy)} · ${esc(r.symbol)} · ${esc(r.label)}:
    ${fmt(+r.summary.total_usd)} USD, Sharpe ${fmt(r.sharpe_annual)}, DSR ${fmt(r.dsr, 3)}</li>`).join('')}</ul>`;
  el.scrollIntoView({behavior: 'smooth'});
};
$('#lb-csv').onclick = () => {
  const keys = ['strategy', 'symbol', 'label', 'path', 'sharpe_annual', 'psr', 'dsr', 'trials', 'max_drawdown_usd', 'longest_underwater_min',
    'profit_factor', 'hit_rate', 'skew', 'kurtosis', 'maker_share', 'captured_spread'];
  const skeys = [...new Set(SHOWN.flatMap(r => Object.keys(r.summary)))];
  const q = v => { const s = String(v ?? ''); return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };
  const csv = [[...keys, ...skeys].join(','), ...SHOWN.map(r => [...keys.map(k => q(r[k])), ...skeys.map(k => q(r.summary[k]))].join(','))].join('\n');
  const a = Object.assign(document.createElement('a'), {href: URL.createObjectURL(new Blob([csv], {type: 'text/csv'})), download: 'leaderboard.csv'});
  a.click();
  URL.revokeObjectURL(a.href);
};

// Equity curve and drawdown, inline SVG.
function curve(cum) {
  if (!cum.length) return '<p class="muted">No minute PnL in this report.</p>';
  const W = 760, H = 220, P = 36, pts = [0, ...cum];
  const lo = Math.min(...pts), hi = Math.max(...pts), span = hi - lo || 1;
  const x = i => P + i * (W - 2 * P) / (pts.length - 1), y = v => H - P - (v - lo) / span * (H - 2 * P);
  let peak = 0, dd = '';
  pts.forEach((v, i) => { peak = Math.max(peak, v); dd += `${i ? 'L' : 'M'}${x(i)},${y(peak)} `; });
  for (let i = pts.length - 1; i >= 0; i--) dd += `L${x(i)},${y(pts[i])} `;
  const line = pts.map((v, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join('');
  return `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="cumulative PnL by minute">
    <path d="${dd}Z" class="dd"/><line x1="${P}" x2="${W - P}" y1="${y(0)}" y2="${y(0)}" class="zero"/>
    <path d="${line}" class="eq"/>
    <text x="${P}" y="14" class="ax">${fmt(hi)} USD</text><text x="${P}" y="${H - 8}" class="ax">${fmt(lo)} USD</text>
    <text x="${W - P}" y="${H - 8}" class="ax" text-anchor="end">${pts.length - 1} min</text></svg>
    <p class="muted">Cumulative PnL by minute; shaded: drawdown from the running peak.</p>`;
}
async function detail(r) {
  const full = await api('/api/report?path=' + encodeURIComponent(r.path));
  Object.assign(full, {dsr: r.dsr, trials: r.trials, sharpe_hurdle_minute: r.sharpe_hurdle_minute});
  const s = full.summary;
  const card = (k, v, c = '') => `<div class="card"><div class="k">${k}</div><div class="v ${c}">${v}</div></div>`;
  const el = $('#detail');
  el.hidden = false;
  el.innerHTML = `<div class="row between"><h2>${esc(full.strategy)} on ${esc(full.symbol)} · ${esc(full.label)}</h2>
    <a href="${full.path}/report.html" target="_blank">HTML report</a></div>
    <div class="cards">${card('PnL', fmt(+s.total_usd) + ' USD', sign(+s.total_usd))}${card('Spread captured', fmt(+s.spread_usd))}
    ${card('Inventory PnL', fmt(+s.inventory_usd))}${card('Fees net of rebates', fmt(+s.fees_usd))}${card('Adverse selection', fmt(+s.adverse_usd))}
    ${card('Sharpe, annualised', fmt(full.sharpe_annual))}${card('PSR (SR&gt;0)', fmt(full.psr, 3))}
    ${card(`DSR, ${full.trials} trial${full.trials > 1 ? 's' : ''}`, fmt(full.dsr, 3))}${card('Max drawdown', fmt(full.max_drawdown_usd) + ' USD')}
    ${card('Longest underwater', full.longest_underwater_min + ' min')}${card('Profit factor', fmt(full.profit_factor))}
    ${card('Hit rate (minutes)', fmt(full.hit_rate * 100, 1) + '%')}${card('Skew / kurtosis', fmt(full.skew) + ' / ' + fmt(full.kurtosis))}
    ${card('Fills', (+s.fills).toLocaleString())}${card('Maker share', fmt(full.maker_share * 100, 0) + '%')}
    ${card('Captured spread / sh', fmt(full.captured_spread, 4))}${card('Max |inventory|', (+s.max_abs_inventory).toLocaleString())}
    ${card('Diverted shares', (+s.diverted_shares).toLocaleString())}
    ${card('PnL 95% interval', fmt(full.pnl_ci_low) + ' to ' + fmt(full.pnl_ci_high))}${card('P(losing day), bootstrap', fmt(full.p_loss * 100, 1) + '%')}</div>
    ${curve(full.curve)}<p class="muted">Interval and loss probability: circular block bootstrap of the minute PnL (2,000 resamples, blocks of about T<sup>1/3</sup> minutes).</p>`;
  el.scrollIntoView({behavior: 'smooth'});
}

// Order book ladder.
let PLAY = null;
const shift = (t, ds) => {
  const [h, m, s] = t.split(':'), sec = Math.max(0, +h * 3600 + +m * 60 + parseFloat(s) + ds);
  const p = n => String(Math.floor(n)).padStart(2, '0');
  return `${p(sec / 3600)}:${p(sec / 60 % 60)}:${p(sec % 60)}`;
};
async function loadBook() {
  const f = $('#book-form');
  try {
    const b = await api('/api/book?' + new URLSearchParams({day: f.day.value, symbol: f.symbol.value.trim().toUpperCase(), time: f.time.value, levels: f.levels.value}));
    $('#book-title').textContent = `${f.symbol.value.toUpperCase()} at ${f.time.value}`;
    $('#book-header').textContent = b.header;
    const max = Math.max(1, ...b.asks.map(l => l.shares), ...b.bids.map(l => l.shares));
    const row = (l, side) => `<div class="lvl ${side}"><span class="px">${l.price.toFixed(4)}</span>
      <span class="bar"><i style="width:${(l.shares / max * 100).toFixed(1)}%"></i></span><span class="q">${l.shares.toLocaleString()}</span></div>`;
    $('#ladder').innerHTML = [...b.asks].reverse().map(l => row(l, 'ask')).join('') + '<div class="mid"></div>' + b.bids.map(l => row(l, 'bid')).join('');
    const ba = b.asks[0]?.price, bb = b.bids[0]?.price, qa = b.asks[0]?.shares, qb = b.bids[0]?.shares;
    const card = (k, v) => `<div class="card"><div class="k">${k}</div><div class="v">${v}</div></div>`;
    const micro = ba && bb ? (ba * qb + bb * qa) / (qa + qb) : null;
    const depth = side => side.reduce((a, l) => a + l.shares, 0);
    const imb = (depth(b.bids) - depth(b.asks)) / (depth(b.bids) + depth(b.asks) || 1);
    $('#book-stats').innerHTML = ba && bb ? card('Mid', fmt((ba + bb) / 2, 4)) + card('Spread', fmt(ba - bb, 4)) +
      card('Micro-price (L1)', fmt(micro, 4)) + card('Depth imbalance', fmt(imb, 3)) : card('Book', 'one-sided or empty');
  } catch (e) { toast(e.message, true); stopPlay(); }
}
const stopPlay = () => { clearInterval(PLAY); PLAY = null; $('#book-play').textContent = 'Play'; };
$('#book-form').onsubmit = e => { e.preventDefault(); loadBook(); };
$('#book-prev').onclick = () => { $('#book-form').time.value = shift($('#book-form').time.value, -1); loadBook(); };
$('#book-next').onclick = () => { $('#book-form').time.value = shift($('#book-form').time.value, 1); loadBook(); };
$('#book-play').onclick = () => {
  if (PLAY) return stopPlay();
  $('#book-play').textContent = 'Pause';
  PLAY = setInterval(() => $('#book-next').onclick(), 1000);
};

// Analysis tools print Markdown: headings, paragraphs, bullets and tables.
function markdown(src) {
  const out = [], ls = src.split('\n');
  const inline = s => esc(s).replace(/\*\*(.+?)\*\*/g, '<b>$1</b>').replace(/`(.+?)`/g, '<code>$1</code>');
  for (let i = 0; i < ls.length; i++) {
    const l = ls[i];
    if (/^\|/.test(l)) {
      const rows = [];
      for (; i < ls.length && /^\|/.test(ls[i]); i++) if (!/^\|[-| :]+\|$/.test(ls[i])) rows.push(ls[i].slice(1, -1).split('|').map(c => c.trim()));
      i--;
      out.push(`<div class="scroll"><table><thead><tr>${rows[0].map(c => `<th>${inline(c)}</th>`).join('')}</tr></thead><tbody>${rows.slice(1).map(r => `<tr>${r.map(c => `<td>${inline(c)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`);
    } else if (/^#{1,4} /.test(l)) out.push(`<h3>${inline(l.replace(/^#+ /, ''))}</h3>`);
    else if (/^- /.test(l)) out.push(`<li>${inline(l.slice(2))}</li>`);
    else if (l.trim()) out.push(`<p>${inline(l)}</p>`);
  }
  return out.join('');
}
function syncAnalysis() {
  const k = $('#an-form [name=kind]').value;
  $$('#an-form [data-for]').forEach(l => l.hidden = !l.dataset.for.split(' ').includes(k));
}
$('#an-form [name=kind]').onchange = syncAnalysis;
$('#an-form').onsubmit = async e => {
  e.preventDefault();
  const f = e.target, kind = f.kind.value;
  const spec = {kind, day: f.day.value.trim(), days: f.day.value.trim(), symbols: f.symbols.value.trim().toUpperCase(),
    pairs: f.pairs.value.trim().toUpperCase(), train: f.train.value.trim(), test: f.test.value.trim()};
  $('#an-out').innerHTML = '';
  const job = await runJob(spec, null, $('#an-status'));
  if (job) $('#an-out').innerHTML = job.lines.some(l => /^[#|]/.test(l)) ? markdown(job.lines.join('\n')) : `<pre class="log">${esc(job.lines.join('\n'))}</pre>`;
};

$('#live-form').onsubmit = e => { e.preventDefault(); runJob({kind: 'live', minutes: e.target.minutes.value}, $('#job-log')); toast('Live run started; the dashboard opens below'); setTimeout(() => $('#dash').src = 'http://127.0.0.1:8088/', 3000); };

syncAnalysis();
refresh().then(loadStrategies);
setInterval(() => { if (STATE?.jobs.some(j => j.running)) refresh(); }, 3000);
show(location.hash.slice(1) || (SNAP ? 'leaderboard' : 'overview'));
