#!/usr/bin/env bash
# Reproducible fetch of Dukascopy XAUUSD M1 bars, 2021-01-01 .. 2025-12-31 (UTC).
#
# Source: Dukascopy Bank public datafeed (https://datafeed.dukascopy.com).
#   Licence: Dukascopy public historical data — free for personal/research use.
#   This is the same source used in the prior ASTRA research (EXP-0019/EXP-0020).
#
# The collector is the open-source `dukascopy-node` CLI (MIT), version pinned
# below. It handles Dukascopy's per-artifact retries, which the raw datafeed
# needs (the public endpoint intermittently returns HTTP 503 under load).
#
# Idempotent: re-running produces the same files. Output is deterministic
# (no wall-clock or random content in the CSVs).
#
# Output (BID, with tick volume):
#   research/data/xauusd_m1/<year>.csv        year = 2021 .. 2025
# Output (ASK, no volume) kept alongside for spread/cost (RULE B):
#   research/data/xauusd_m1/ask/<year>.csv
#
# Large raw CSVs are .gitignore'd; only fetch.sh + checksums + a 1000-row
# sample per year are committed. See README.md.

set -euo pipefail

DUKASCOPY_NODE_VERSION="1.50.0"
FROM_DATE="2021-01-01"
TO_DATE="2025-12-31"
INSTRUMENT="xauusd"
TIMEFRAME="m1"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${ROOT_DIR}/.work"
mkdir -p "${WORK_DIR}"

# Pin and install the collector locally (no global install).
if [ ! -f "${WORK_DIR}/node_modules/dukascopy-node/package.json" ]; then
  ( cd "${WORK_DIR}" \
    && npm init -y >/dev/null 2>&1 \
    && npm install "dukascopy-node@${DUKASCOPY_NODE_VERSION}" --no-audit --no-fund )
fi

run_download() {
  local price_type="$1" out_dir="$2" volumes="$3"
  mkdir -p "${out_dir}"
  local vol_flag=""
  [ "${volumes}" = "1" ] && vol_flag="-v"
  for year in 2021 2022 2023 2024 2025; do
    echo "== ${price_type} ${year} =="
    ( cd "${WORK_DIR}" && npx --no-install dukascopy-node \
        -i "${INSTRUMENT}" \
        -from "${year}-01-01" -to "${year}-12-31" \
        -t "${TIMEFRAME}" -p "${price_type}" ${vol_flag} \
        -f csv -dir "${out_dir}" \
        -r 8 -rp 1500 -re -fr -s )
    mv "${out_dir}/xauusd-m1-${price_type}-${year}-01-01-${year}-12-31.csv" \
       "${out_dir}/${year}.csv"
  done
}

# BID with tick volume -> primary OHLCV.
run_download bid "${ROOT_DIR}" 1
# ASK without volume -> spread/cost reference.
run_download ask "${ROOT_DIR}/ask" 0

echo "done: ${ROOT_DIR}"
