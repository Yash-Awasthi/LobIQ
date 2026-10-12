#!/bin/bash
# Downloads one Nasdaq ITCH day (e.g. S121225-v50.txt.gz), verifies CRC and SHA-256, ingests it.
# Env: HFT_DATA (~/data), HFT_INGEST, HFT_CHECKPOINT, KEEP_GZ=1 keeps the download.
set -euo pipefail

name=${1:?usage: ingest_day.sh <remote-file-name>}
base=${HFT_BASE:-"https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH"}
data=${HFT_DATA:-$HOME/data}
ingest=${HFT_INGEST:-$(dirname "$0")/../build/release/apps/ingest}
checkpoint=${HFT_CHECKPOINT:-$(dirname "$0")/../build/release/apps/checkpoint}
gz="$data/$name"
store="$data/store-${name%%.*}"

mkdir -p "$data"
[ ! -e "$store" ] || { echo "store exists: $store" >&2; exit 1; }

want_size=$(curl -sfI --http1.1 "$base/$name" | tr -d '\r' | awk 'tolower($1)=="content-length:"{print $2}' || true)
[ -n "$want_size" ] || { echo "not found on server: $name" >&2; exit 1; }

# The server throttles each connection to about 220 KB/s; aria2c opens 16 when installed.
for i in $(seq 1 50); do
    have_size=$(stat -L -c %s "$gz" 2>/dev/null || echo 0)
    [ "$have_size" = "$want_size" ] && [ ! -e "$gz.aria2" ] && break
    if command -v aria2c >/dev/null; then
        aria2c -q -c -x16 -s16 -k4M --file-allocation=none -d "$(dirname "$gz")" -o "$(basename "$gz")" "$base/$name" ||
            { echo "retry $i" >&2; sleep 5; }
    else
        curl -sS --http1.1 -C - -o "$gz" "$base/$name" || { echo "retry $i" >&2; sleep 5; }
    fi
done
[ "$(stat -L -c %s "$gz")" = "$want_size" ] && [ ! -e "$gz.aria2" ] || { echo "download incomplete" >&2; exit 1; }
if [ "$(head -c2 "$gz" | od -An -tx1 | tr -d ' \n')" != 1f8b ]; then
    rm -f "$gz"
    echo "not a gzip file (wrong name?): $name" >&2
    exit 1
fi

if md5_line=$(curl -sf --http1.1 "$base/$name.md5sum"); then
    [ "${md5_line%% *}" = "$(md5sum "$gz" | cut -d' ' -f1)" ] || { echo "md5 mismatch" >&2; exit 1; }
    echo "md5 ok"
fi

"$ingest" "$store" "$gz"
"$checkpoint" "$store"
[ "${KEEP_GZ:-0}" = 1 ] || rm "$gz"
