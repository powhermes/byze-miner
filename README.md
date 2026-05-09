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
  - `--pool-difficulty N` (optional): fix local share difficulty multiplier if the pool does not send `mining.set_difficulty`
- Live stats:
  - hashrate
  - accepted shares
  - rejected shares
- Submits when RandomX hash meets **pool share target** from `mining.set_difficulty` (easier than a full block); still submits full block hex for both pool shares and block solutions (pool validates and forwards blocks only when network target is met).
- Uses the Byze RandomX key; **RandomX is vendored** under `third_party/randomx` (upstream Monero RandomX). No Byze daemon source checkout is required.

## Build

**Debian / Ubuntu (installs dependencies, then builds):**

```bash
git clone <byze-miner-url>
cd byze-miner
chmod +x scripts/bootstrap-debian.sh
./scripts/bootstrap-debian.sh
# -> ./build/byze-miner
```

**Already have build tools:** configure Boost, OpenSSL, CMake 3.20+, and a C++20 compiler, then:

```bash
./scripts/build.sh
```

Pass extra CMake flags after either script (they are forwarded to the configure step), e.g. `./scripts/bootstrap-debian.sh -DCMAKE_BUILD_TYPE=Release`.

## Run

```bash
./build/byze-miner \
  --pool 127.0.0.1:3333 \
  --wallet YOUR_WALLET_ADDRESS \
  --worker rig01 \
  --threads 4
```

## Architecture Proof Milestone (No Qt)

Use the proof script to validate this exact pipeline end-to-end:

1. build valid block candidate  
2. RandomX hash block header  
3. detect valid share  
4. `mining.submit`  
5. sidecar accepts  
6. daemon `submitblock` path advances chain

```bash
./scripts/prove_architecture.sh \
  127.0.0.1:3333 \
  YOUR_WALLET_ADDRESS \
  rig01 \
  2 \
  /home/byze/byze/src/byze-cli
```

The script prints step markers from miner stdout and verifies block height increased.
