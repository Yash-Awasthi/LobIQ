#!/usr/bin/env bash
# Lead-lag between related products (docs/results/lead-lag.md) on the December days.
set -euo pipefail
d=${HFT_DATA:-$HOME/data}
b=${HFT_BUILD:-$(dirname "$0")/../build/native}
days=${DAYS:-S120825 S120925 S121025 S121125 S121225}
stores=$(for x in $days; do printf '%s,' "$d/store-$x-v50"; done)
"$b/apps/itch_leadlag" "${stores%,}" ${PAIRS:+"$PAIRS"}
