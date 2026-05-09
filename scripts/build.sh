#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" "$@"
cmake --build "${ROOT_DIR}/build" -j"$(nproc 2>/dev/null || echo 4)"
echo "Built: ${ROOT_DIR}/build/byze-miner"
