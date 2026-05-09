#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BYZE_SRC="${BYZE_SOURCE_DIR:-/home/byze/byze}"

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" -DBYZE_SOURCE_DIR="${BYZE_SRC}"
cmake --build "${ROOT_DIR}/build" -j"$(nproc 2>/dev/null || echo 4)"
echo "Built: ${ROOT_DIR}/build/byze-miner"
