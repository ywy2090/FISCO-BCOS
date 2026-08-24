#!/usr/bin/env bash
# setup_mainnet_stack.sh — 同步 OP Sepolia op-geth + op-node 栈。
#
# 用法:  bash tools/op-mainnet-replay/setup_mainnet_stack.sh
#
# 环境变量: OPGETH_BIN / OPNODE_BIN / L1_RPC / L1_BEACON / DATA / JWT
#   缺省见下。op-geth / op-node 二进制须已安装（版本 pin 见 versions.json）。
#
#  ⚠️ 窗口现实（2026-08 起）：OP Sepolia 已于 2026-06-17 激活 Karst
#  （timestamp 1781712001），Jovian 语义窗口 (1763568001, 1781712001) 是历史区间
#  （~2.5 个月前）。重放引导状态取自 h0-1（窗口首块前一块）——snap 同步只保留
#  近 128 块状态，Jovian 窗口必须 FULL 同步 + archive 保留（gcmode archive）。
#  数据量数百 GB；请确认 DATA 所在卷有足够空间（本机主卷仅 ~20Gi，需外接盘/远程）。
set -euo pipefail
OPGETH_BIN="${OPGETH_BIN:-/usr/local/bin/op-geth}"
OPNODE_BIN="${OPNODE_BIN:-/usr/local/bin/op-node}"
L1_RPC="${L1_RPC:-https://ethereum-sepolia-rpc.publicnode.com}"
L1_BEACON="${L1_BEACON:-https://ethereum-sepolia-beacon-api.publicnode.com}"
DATA="${DATA:-$HOME/op-sepolia-data}"
JWT="${DATA}/jwt.hex"
mkdir -p "$DATA"
[ -s "$JWT" ] || openssl rand -hex 32 > "$JWT"

# op-geth 链名是复合键 op-sepolia（--op-network mainnet/op-mainnet 均错）。
# FULL 同步（非 snap）+ archive：Jovian 窗口状态需历史保留（见头部警告）。
"$OPGETH_BIN" --datadir "$DATA/geth" --syncmode full --gcmode archive \
  --http --http.port 8545 --http.api eth,debug,net,web3 \
  --authrpc.jwtsecret "$JWT" --op-network op-sepolia \
  --rollup.sequencerhttp https://sepolia-sequencer.optimism.io \
  > "$DATA/geth.log" 2>&1 & echo $! > "$DATA/geth.pid"

"$OPNODE_BIN" --network sepolia --l1 "$L1_RPC" --l1.beacon "$L1_BEACON" \
  --l2 http://127.0.0.1:8551 --l2.jwt-secret "$JWT" \
  --rpc.addr 127.0.0.1 --rpc.port 9545 \
  > "$DATA/opnode.log" 2>&1 & echo $! > "$DATA/opnode.pid"

echo "op-geth pid $(cat "$DATA/geth.pid"), op-node pid $(cat "$DATA/opnode.pid")"
echo "同步完成判定: eth_syncing=false；archive 模式才能 dump Jovian 窗口的非 tip 高度状态"
