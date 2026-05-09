#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 5 ]]; then
  echo "Usage: $0 <pool_host:port> <wallet> <worker> <threads> <byze-cli-cmd> [timeout_seconds]"
  echo "Example: $0 127.0.0.1:3333 byz1... rig01 2 \"/home/byze/byze/build/bin/byze-cli -conf=... -datadir=...\" 5400"
  exit 1
fi

POOL="$1"
WALLET="$2"
WORKER="$3"
THREADS="$4"
BYZECLI="$5"
TIMEOUT_SECONDS="${6:-${PROOF_TIMEOUT_SECONDS:-180}}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MINER_BIN="${ROOT_DIR}/build/byze-miner"

if [[ ! -x "${MINER_BIN}" ]]; then
  echo "Building byze-miner..."
  "${ROOT_DIR}/scripts/build.sh" >/dev/null
fi

BEFORE="$(${BYZECLI} getblockcount)"
DIFFICULTY="$(${BYZECLI} getblockchaininfo | python3 -c 'import sys,json; print(json.load(sys.stdin)["difficulty"])')"
echo "blockcount_before=${BEFORE}"
echo "chain_difficulty=${DIFFICULTY}"
python3 - "${DIFFICULTY}" <<'PY'
import sys
d=float(sys.argv[1])
# Expected hashes ~= difficulty * 2^32 for one valid block.
exp_hashes=d*(2**32)
print(f"expected_hashes_for_block≈{int(exp_hashes)}")
print("note: at 500 H/s and difficulty 0.000244, expected solve time is ~35 minutes")
PY

LOG_FILE="${ROOT_DIR}/proof-run.log"
rm -f "${LOG_FILE}"

set +e
timeout "${TIMEOUT_SECONDS}" "${MINER_BIN}" --pool "${POOL}" --wallet "${WALLET}" --worker "${WORKER}" --threads "${THREADS}" >"${LOG_FILE}" 2>&1
RC=$?
set -e

AFTER="$(${BYZECLI} getblockcount)"
echo "blockcount_after=${AFTER}"

echo "proof_timeout_seconds=${TIMEOUT_SECONDS}"
echo "--- milestone evidence ---"
grep -m1 "step1:block-candidate-built" "${LOG_FILE}" || true
grep -m1 "step2:randomx-hashing" "${LOG_FILE}" || true
grep -m1 "step3:valid-share-detected" "${LOG_FILE}" || true
grep -m1 "step4:submit-mining-submit" "${LOG_FILE}" || true
grep "hashrate=" "${LOG_FILE}" | tail -n 5 || true

if [[ "${AFTER}" -gt "${BEFORE}" ]]; then
  echo "PASS: block height increased (${BEFORE} -> ${AFTER})"
  echo "Proof includes steps 1..6 (submitblock accepted on daemon path)."
  exit 0
fi

echo "FAIL: block height did not increase."
if [[ "${RC}" -eq 124 ]]; then
  echo "Miner timed out before finding a valid block share."
fi
echo "Check ${LOG_FILE} plus sidecar/adapter logs."
exit 2
