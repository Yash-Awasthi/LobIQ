#!/usr/bin/env bash
# Microstructure study (docs/results/microstructure.md): fits on the first two December days,
# scores on the last two. Needs the stores from scripts/ingest_day.sh.
set -euo pipefail
d=${HFT_DATA:-$HOME/data}
b=${HFT_BUILD:-$(dirname "$0")/../build/native}
"$b/apps/itch_study" --train "$d/store-S120825-v50,$d/store-S120925-v50" \
                     --test "$d/store-S121025-v50,$d/store-S121125-v50" --symbols "${SYMBOLS:-30}"
