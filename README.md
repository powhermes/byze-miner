# byze-miner

Standalone CPU miner for Byze pool mining (no XMRig dependency).

## Features

- Stratum adapter compatibility:
  - `mining.subscribe`
  - `mining.authorize`
  - `mining.submit`
- CLI flags:
  - `--pool host:port`
  - `--wallet <address>`
  - `--worker <name>`
  - `--threads <count>`
- Live stats:
  - hashrate
  - accepted shares
  - rejected shares
- Uses Byze RandomX key/mode and links RandomX from the Byze source tree.

## Build

```bash
cmake -S . -B build -DBYZE_SOURCE_DIR=/home/byze/byze
cmake --build build -j
```

## Run

```bash
./build/byze-miner \
  --pool 127.0.0.1:3333 \
  --wallet YOUR_WALLET_ADDRESS \
  --worker rig01 \
  --threads 4
```
