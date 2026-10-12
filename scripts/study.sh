#!/usr/bin/env bash
# Microstructure study (docs/results/microstructure.md): fits on two December days, scores on
# the next three and on a day six months later (TRAIN, TEST: comma-separated day names). Needs the stores from scripts/ingest_day.sh.
set -euo pipefail
d=${HFT_DATA:-$HOME/data}
b=${HFT_BUILD:-$(dirname "$0")/../build/native}
train=${TRAIN:-S120825,S120925}
test=${TEST:-S121025,S121125,S121225,S061226}
stores() { local out= x; for x in ${1//,/ }; do out+="${out:+,}$d/store-$x-v50"; done; echo "$out"; }
"$b/apps/itch_study" --train "$(stores "$train")" --test "$(stores "$test")" --symbols "${SYMBOLS:-30}"
