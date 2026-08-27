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

## Karst 部署门（operator）

Karst 上线是**原子批次**：执行层 schedule、Engine profile、Osaka 预编译与 NUT 激活语义
必须同版本一起发布；Task 7 的 `op-karst-release-gate` 扫描禁止 legacy fork API
（`isJovianActive` / `OpForkFlags` / `configAt(OpForkFlags)`）残留在生产 OP 面。

### 新链 genesis

1. 在 genesis ini 写入完整 `[opstack].fork_schedule`（canonical 字符串，示例见
   `tools/opstack-genesis/chain-config.template.yaml`）。新空库**必须**带 schedule；
   与 `feature_op_jovian` / `executor.evm_revision*` 互斥。
2. 启动前核对 `keccakOpForkScheduleHash(schedule)` 与链上 metadata 三件套
   （`op_fork_schedule` / `op_fork_schedule_hash` / `op_fork_schedule_genesis_hash`）
   一致；各 EL 节点与 op-node 的 schedule 字符串及 hash **必须逐字节相同**。
3. 已知 Sepolia Karst 新链 baseline：`0:jovian,1781712001:karst` → hash
   `1600c14baa71d58ae7f8d3c07ff24a73e26b209e6d491577a34b879ce2c6df12`。
   Mainnet 形态：`0:isthmus,1764691201:jovian,1783526401:karst`（hash 由工具计算）。

### 存量链离线迁移

```bash
# 仅 KeyPage V3_18 + 未加密 RocksDB；见 --help 的 Limitations
./build/tools/op-fork-schedule-migrate/op-fork-schedule-migrate \
  --storage <datadir>/group0/data \
  --schedule '0:jovian,<karst_ts>:karst' \
  --dry-run
# 确认 old/new canonical + hash、history_freeze_tip_timestamp_seconds 后再 --yes
```

- 校验规则：已激活前缀必须与旧 schedule 完全一致；仅允许**未来** fork 变更。
- 冻结边界是 **ledger tip**（CLI 无 Engine safe/finalized 视图）。
- 迁移写入 metadata 三件套；ini 中 `fork_schedule` 若与 DB 不一致则节点拒绝启动。
- **不可逆**：首个 safe/finalized Karst 块之后禁止回滚 schedule 或降级二进制；
  无在线迁移、无块历史重写。

### 发布前回归（Task 11 gate）

```bash
rtk git diff --check
rtk cmake --build build --target bcos-evm-opstack-tests \
  opstack-executor-block-tests opstack-executor-detail-tests \
  test-bcos-engine test-bcos-rpc test-bcos-tool test-bcos-ledger \
  op-fork-schedule-migrate opstack-mainnet-replay op-karst-release-gate -j8
rtk ctest --test-dir build -R \
  'BcosEvmOpstackTests|OpstackExecutorBlockTests|OpstackExecutorDetailTests|OpKarstReleaseGate|ReplayCli|OpMainnetReplay|test-bcos-tool|test-ethereum-state-smoke|EthereumExecutorDiffTests' \
  --output-on-failure
```

### Karst 重放验收（op-reth oracle）

- Jovian 窗口仍用 op-geth `e8800cff` + `opt8n-ref --live`；**Karst 拒绝 op-geth**，
  须 pinned op-reth archive + `run_sync.sh --fork karst`（见 `versions.json`）。
- 逐级：`--count 1` → `2` → `100` → `900`；记录 report.json、sidecar 大小、耗时。
- 部署证据模板：FISCO commit、optimism `5f90f749`、specs `689a96f6`、NUT SHA-256
  `08f5df36…`、canonical schedule + 各节点 keccak hash、重放摘要。

## 测试与回归

```bash
cmake --build build --target opstack-executor-block-tests opstack-mainnet-replay -j 8
./build/opstack-executor/tests/opstack-executor-block-tests          # 全部单测
bash opstack-executor/tests/t8n/generator/ensure-vectors.sh          # corpus 回归
cmake --build build --target op-karst-release-gate -j8               # Karst 原子门
```

## 延后项

- **5k+ 全量**：`run_sync.sh` 的 `--to latest` 放大即可（RocksDB 锚点已备）。
- **连续哨兵**：`run_sync.sh` 挂 cron + 报警（Task 8 已是最小闭环）。
- **Karst live archive**：离线代码与 pinned op-reth 路径已交付（Task 10）；live 1/2/100/900
  块验收需外接 archive 存储 + 同步 op-reth（当前 BLOCKED）。
- **跨 fork 回填**：不建议（FISCO 建模起点 ecotone；fork 切换模型需先改造
  `OpForkSchedule.cpp::configAt()`）。

## 已确认的已知权衡（2026-08-24 review，不改代码）

- **每块全状态 MPT 重建**（`stateRootOf` 每块 O(全状态)）：`StateRootCompute.h`
  明确 correctness-first，是设计取舍；Go 侧 Process 已逐块验 root，C++ 侧重算为
  防御层。大窗口的优化路径（增量 trie / 仅首末块全根）留待后续。
- **单一 RPC 全链路信任**：header/状态/期望值全部来自同一节点，Process 交叉校验
  只证执行一致性（stateRoot/gasUsed/receiptsRoot/bloom），不验 verifyHeader 类
  共识字段（baseFee 派生/txRoot/extraData）。工具定位为"对诚实节点的差异发现"，
  不声称可对抗篡改 RPC；多节点交叉核对留待后续。
- **`get_account` storage 前缀探测 / `applyDiff` 全码 keccak**：每账户读放大
  约 2×，热路径微优化（写入侧维护 storage 存在位）留待后续。
- **hex 转换与 bcos-utilities `toQuantity` 语义重复**：低频路径，intx 类型差异
  使部分无法直接复用。

## 交付物

- `opstack-executor/tests/support/ReplayGate.h` — 共享重放机制（boost-free，泛化 StateBackend）
- `opstack-executor/tests/support/StateBackendRocksDB.{h,cpp}` — RocksDB StateView + sidecar + 锚点
- `tools/op-mainnet-replay/replay_cli.cpp` — 零容忍重放 CLI（--chain-id/--skip-poststate/
  --check-import-root/--allowlist/--report）
- `opstack-executor/tests/t8n/generator/live.go` — `opt8n-ref --live`（accountRange 流式 +
  header 交叉校验 + BLOCKHASH 预置 + --to latest）
- `tools/op-mainnet-replay/setup_mainnet_stack.sh` / `run_sync.sh` / `versions.json`
