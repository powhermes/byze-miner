#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MINER_BIN="${ROOT_DIR}/build/byze-miner"
SERVICE_NAME="byze-miner.service"
SYSTEMD_DIR="/etc/systemd/system"
TARGET="${SYSTEMD_DIR}/${SERVICE_NAME}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root (e.g. sudo $0)" >&2
  exit 1
fi

if [[ ! -x "${MINER_BIN}" ]]; then
  echo "Missing ${MINER_BIN}. Run ${ROOT_DIR}/scripts/build-byze-miner.sh first." >&2
  exit 1
fi

read -r -p "Pool host:port [127.0.0.1:3333]: " POOL
POOL="${POOL:-127.0.0.1:3333}"
read -r -p "Mining address (wallet / payout): " MINING_WALLET
if [[ -z "${MINING_WALLET}" ]]; then
  echo "Mining address required" >&2
  exit 1
fi
read -r -p "Worker name [rig01]: " WORKER_NAME
WORKER_NAME="${WORKER_NAME:-rig01}"
read -r -p "CPU threads [4]: " THREADS
THREADS="${THREADS:-4}"
read -r -p "Run as unix user [byze]: " RUN_USER
RUN_USER="${RUN_USER:-byze}"

cat >"${TARGET}" <<EOF
[Unit]
Description=Byze RandomX CPU miner (stratum)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_USER}
ExecStart=${MINER_BIN} --pool ${POOL} --wallet ${MINING_WALLET} --worker ${WORKER_NAME} --threads ${THREADS}
Restart=always
RestartSec=5
KillMode=mixed
TimeoutStopSec=30
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
echo "Installed ${TARGET}"
echo "  sudo systemctl start ${SERVICE_NAME}"
echo "  sudo journalctl -u ${SERVICE_NAME} -f"
