#!/usr/bin/env bash
# run_sync.sh — 指定快高同步 OP Sepolia 近期流量（Karst 前 Jovian 窗口）。
#
# 用法:  run_sync.sh [--from <h0>]
#    --from <h0>   首次/指定起点（状态在 h0-1 引导）；之后无参数 = 读锚点续跑
#                  [anchor+1, latest]，锚点文件推进。
#
# 窗口约束（D11 / versions.json）：所有重放块 timestamp ∈ (jovian=1763568001,
# karst=1781712001)。2026-08 起 tip 已越过 Karst——本脚本的 --to latest 会取到
# Karst 块并被生成器窗口校验拒绝；须显式 --to 停在 Karst 前（或用历史 --from
# 窗口，节点须 archive 保留该高度状态）。
set -euo pipefail
RPC="${RPC:-http://127.0.0.1:8545}"
OUT="${OUT:-/tmp/op-replay-sync}"
KARST=1781712001
mkdir -p "$OUT"

FROM=""
if [ "${1:-}" = "--from" ]; then
  FROM="$2"
elif [ -f "$OUT/anchor.txt" ]; then
  FROM=$(awk '{print $1}' "$OUT/anchor.txt")
  FROM=$((FROM + 1))
fi

LATEST=$(curl -s -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"eth_blockNumber","params":[],"id":1}' "$RPC" \
  | python3 -c 'import sys,json; print(int(json.load(sys.stdin)["result"],16))')

if [ -z "${FROM:-}" ]; then
  FROM=$((LATEST - 99))
fi

# 窗口校验：latest 块 timestamp 必须 < karst，越界报错（生成器同样强制）。
LATEST_TS=$(curl -s -X POST -H "Content-Type: application/json" \
  -d "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"0x$(printf '%x' "$LATEST")\",false],\"id\":1}" "$RPC" \
  | python3 -c 'import sys,json; print(int(json.load(sys.stdin)["result"]["timestamp"],16))')
if [ "$LATEST_TS" -ge "$KARST" ]; then
  echo "ERROR: tip timestamp $LATEST_TS >= karst $KARST — Jovian 窗口已结束，请显式 --to 停在 Karst 前" >&2
  exit 1
fi

echo "sync window: [$FROM, $LATEST] (latest ts $LATEST_TS < karst $KARST)"
opt8n-ref --live "$RPC" --from "$FROM" --to latest --fork jovian \
  --out "$OUT/chain.json" --sidecar "$OUT/state.sidecar"
./build/opstack-executor/tests/opstack-mainnet-replay --chain "$OUT/chain.json" \
  --sidecar "$OUT/state.sidecar" --skip-poststate --chain-id 11155420 \
  --report "$OUT/report.json" || { echo "DIVERGENCE DETECTED"; exit 1; }
echo "$LATEST <stateRoot>" > "$OUT/anchor.txt"   # 推进锚点
echo "anchor advanced to $LATEST"
