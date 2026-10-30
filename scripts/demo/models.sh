#!/usr/bin/env bash
# Demo (f): the forecasting models and the options engine on bundled or synthetic data, one HTML page
# each plus an index: volatility, Hawkes order flow, regimes and the alpha model (with symbolic
# regression) fitted on one synthetic day and scored on another, the SVI surface of the sample option
# chain, and one option priced four ways. Each page is checked for what it must show; exit 1 if not.
# Usage: models.sh [out-dir]
set -euo pipefail
cd "$(dirname "$0")/../.."
B=${LOBIQ_BIN:-build/release/apps}
out=${1:-out/models}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out"
cp studio/static/viz.js studio/static/style.css "$out/"  # the pages draw their charts with the studio's renderer
for s in 7 8; do "$B/gen_day" $s 390 2>/dev/null | "$B/ingest" "$tmp/day$s" >/dev/null; done
days=(--train "$tmp/day7" --test "$tmp/day8" --symbols QRSIM)
fail=0
page() {  # name, title, check (a grep pattern the page must contain), command...
    local name=$1 title=$2 want=$3
    shift 3
    "$@" 2>/dev/null > "$tmp/$name.md" || true
    python3 scripts/demo/md2html.py "$title" < "$tmp/$name.md" > "$out/$name.html"
    if grep -q -- "$want" "$tmp/$name.md" && ! grep -qiw 'nan\|inf' "$tmp/$name.md"; then
        echo "$name: $out/$name.html"
    else
        echo "$name: missing '$want' or has nan/inf"
        fail=1
    fi
}
page vol "Volatility forecasts" "^## Keys for avellaneda_stoikov" "$B/itch_vol" "${days[@]}"
page hawkes "Multivariate Hawkes order flow" "^## Who triggers whom" "$B/itch_hawkes" "${days[@]}"
page regime "Market regimes" "^## Regimes on the test days" "$B/itch_regime" "${days[@]}"
page alpha "Alpha model" "^## Symbolic regression" "$B/itch_alpha" "${days[@]}" --symbolic 20
page surface "Implied volatility surface" "at 0 of 303 grid points" "$B/options" surface data/sample/options_chain.csv --rate 0.04 --greeks
page price "Option pricing" "^| Binomial, American" "$B/options" price put 100 105 1 0.05 0 0.25
cat > "$out/index.html" <<'EOF'
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LobIQ forecasting models</title>
<style>
:root{--bg:#fbfbfa;--fg:#1d1d1b;--muted:#6b6b66;--line:#e2e1dc;--card:#fff;--link:#2563eb}
@media(prefers-color-scheme:dark){:root{--bg:#141413;--fg:#ecebe6;--muted:#9c9b95;--line:#2e2e2b;--card:#1d1d1b;--link:#60a5fa}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.55 system-ui,sans-serif}
main{max-width:760px;margin:0 auto;padding:32px 16px 48px}h1{font-size:24px;margin:0 0 4px}
p{margin:8px 0}.muted{color:var(--muted)}a{color:var(--link)}
.card{display:block;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:12px 0;text-decoration:none;color:inherit}
.card b{color:var(--link)}code{font-size:13px}
</style></head>
<body><main>
<p><a href="../index.html">All demo reports</a></p>
<h1>Forecasting models and options</h1>
<p class="muted">Fitted on one synthetic Nasdaq day and scored on another (queue-reactive order flow, so the
results describe the simulator, not a market). Run them on real days with <code>lobiq vol</code>,
<code>hawkes</code>, <code>regime</code> and <code>alpha</code>, or from the studio's Analysis tab. Formulas: docs/METHODS.md.</p>
<a class="card" href="vol.html"><b>Volatility forecasts</b><p>GARCH(1,1) with normal and Student-t innovations and HAR on
realized variance, against the market makers' EWMA by QLIKE with Diebold-Mariano statistics; the GARCH keys
for the Avellaneda-Stoikov maker.</p></a>
<a class="card" href="hawkes.html"><b>Order flow as a multivariate Hawkes process</b><p>Market orders, limit orders and
cancels on each side, fitted by EM: branching ratio, who triggers whom, out-of-sample gain over Poisson and a
time-rescaling check.</p></a>
<a class="card" href="regime.html"><b>Market regimes</b><p>A Gaussian hidden Markov model over minute features, states
chosen by BIC, test minutes labelled by the forward filter, each regime described by what came next.</p></a>
<a class="card" href="alpha.html"><b>Alpha model</b><p>Ridge on the backtester's streaming features with purged,
embargoed cross-validation and conformal intervals, and a symbolic regression search for a closed-form formula.</p></a>
<a class="card" href="surface.html"><b>Implied volatility surface</b><p>The bundled option chain: forwards by put-call
parity, Black-76 implied volatilities, an SVI slice per expiry with butterfly and calendar arbitrage checks,
Greeks per quote.</p></a>
<a class="card" href="price.html"><b>Option pricing</b><p>One put by Black-Scholes, a 2,000-step binomial tree (European and
American) and Monte Carlo with its standard error, with every Greek and the early exercise premium.</p></a>
</main></body></html>
EOF
echo "models: $out/index.html"
exit $fail
