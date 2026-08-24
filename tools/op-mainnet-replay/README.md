# op-mainnet-replay — OP Sepolia 真实区块重放验证体系

用 OP Sepolia 的**真实区块**验证 FISCO opstack 执行一致性：op-geth 侧推导全字段 →
RocksDB 状态后端流式导入 → 逐块零容忍比对 → 锚点续跑追链。

```
Sepolia 栈(op-geth archive + op-node, FULL+archive 同步)
  ├─ debug_accountRange 流式 → state.sidecar → RocksDB StateView (CLI --sidecar)
  └─ eth_getBlockByNumber + op-geth 库内 StateProcessor.Process 推导
       → chain.json (v3-block, 无 postState) + 真实 header 交叉校验 (opt8n-ref --live)
            └─ opstack-mainnet-replay: 逐块零容忍 → report.json
                 → 差异分流 → 四元组 allowlist 豁免
```

## 快速开始

```bash
# 1. 同步 OP Sepolia 节点（archive；数据量大，确认磁盘）
bash tools/op-mainnet-replay/setup_mainnet_stack.sh

# 2. 从指定快高生成 + 重放（首次）
bash tools/op-mainnet-replay/run_sync.sh --from <h0>
#    之后无参数 = 读锚点续跑 [anchor+1, latest]

# 2'. 手工步骤（生成器在 op-geth checkout 内构建，见下）
opt8n-ref --live http://127.0.0.1:8545 --from <h0> --count 100 --fork jovian \
  --out /tmp/live.json --sidecar /tmp/live.sidecar --op-geth-commit e8800cff...
./build/opstack-executor/tests/opstack-mainnet-replay --chain /tmp/live.json \
  --sidecar /tmp/live.sidecar --skip-poststate --chain-id 11155420 \
  --check-import-root <sidecar ROOT 值> --report /tmp/report.json
```

## 窗口约束（D8/D11）

- **chainId 11155420**；重放块 timestamp ∈ **(jovian=1763568001, karst=1781712001)**，
  全 Jovian 语义（FISCO 对 Jovian 三层全支持，执行层零改造）。
- ⚠️ **2026-08 起 tip 已越过 Karst（2026-06-17 激活）**：Jovian 窗口是历史区间，
  须显式选 Karst 前高度 + 节点 `--gcmode archive` 保留该高度状态（snap 只保留近
  128 块）。`--to latest` 会取到 Karst 块并被窗口校验拒绝（预期行为）。
- 金标准 op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`（v1.101702.2，EOL
  2026-05-31，只懂到 Jovian）——与窗口匹配。

## 交叉校验（防"两侧同错假绿"）

`opt8n-ref --live` 把真实 h0-1 头作为锚点，用 `core.StateProcessor.Process` 重执行
每个真实块：产出 stateRoot / gasUsed / receiptsRoot / logsBloom 必须与真实 header
逐字段一致（等价 op-geth 的 ValidateState），任何不一致即失败退出，绝不产出错向量。

## FINDING-2（执行层唯一可能必修）

EIP-7702 退款判据 `Eip7702Recover.h:124` 与 op-geth 的 `Exist(authAddr)` 不一致
（PENDING-FIX）。Sepolia 窗口内有 7702 交易，试点触发即 gasUsed/stateRoot 发散；
修 1–2 人日。差异确认真实则进四元组 allowlist（`--allowlist`，
`{"<vectorId>.<field>": [["<want>","<got>","STATUS"]]}`，STATUS=PENDING-FIX|SIGNED-OFF）。

## 生成器构建（live.go 在 op-geth checkout 内编译）

```bash
cd <op-geth@e8800cffe>
rm -rf cmd/opt8n-ref; cp -r <repo>/opstack-executor/tests/t8n/generator cmd/opt8n-ref
go build -o opt8n-ref ./cmd/opt8n-ref     # go.mod 需 go 1.24（GOTOOLCHAIN 自动下载）
```

## 测试与回归

```bash
cmake --build build --target opstack-executor-block-tests opstack-mainnet-replay -j 8
./build/opstack-executor/tests/opstack-executor-block-tests          # 全部单测
bash opstack-executor/tests/t8n/generator/ensure-vectors.sh          # corpus 回归
```

## 延后项

- **5k+ 全量**：`run_sync.sh` 的 `--to latest` 放大即可（RocksDB 锚点已备）。
- **连续哨兵**：`run_sync.sh` 挂 cron + 报警（Task 8 已是最小闭环）。
- **Karst**：需实现 Karst 语义（31 条 NUT 升级 + gas 膨胀）——另立评估。
- **跨 fork 回填**：不建议（FISCO 建模起点 ecotone；fork 切换模型需先改造
  `OpForkSchedule.cpp::configAt()`）。

## 交付物

- `opstack-executor/tests/support/ReplayGate.h` — 共享重放机制（boost-free，泛化 StateBackend）
- `opstack-executor/tests/support/StateBackendRocksDB.{h,cpp}` — RocksDB StateView + sidecar + 锚点
- `tools/op-mainnet-replay/replay_cli.cpp` — 零容忍重放 CLI（--chain-id/--skip-poststate/
  --check-import-root/--allowlist/--report）
- `opstack-executor/tests/t8n/generator/live.go` — `opt8n-ref --live`（accountRange 流式 +
  header 交叉校验 + BLOCKHASH 预置 + --to latest）
- `tools/op-mainnet-replay/setup_mainnet_stack.sh` / `run_sync.sh` / `versions.json`
