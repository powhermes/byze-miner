#!/usr/bin/env bash
# Debian/Ubuntu: install build dependencies, make scripts executable, then build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if ! command -v apt >/dev/null 2>&1; then
  echo "This script expects Debian/Ubuntu with apt." >&2
  exit 1
fi

sudo apt update
sudo apt install -y build-essential cmake git libboost-all-dev libssl-dev pkg-config

chmod +x "${ROOT_DIR}/scripts/"*.sh

exec "${ROOT_DIR}/scripts/build.sh" "$@"
