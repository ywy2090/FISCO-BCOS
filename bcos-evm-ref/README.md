# bcos-evm-ref

Spec: `bcos-evm/docs/superpowers/specs/2026-07-09-bcos-evm-ref-rev8-opstack-foundation-design.md` (rev.8.2)
（前置：`2026-07-08-bcos-evm-ref-evmone-reuse-design.md` rev.7，evmone 基线 / §4.3 OP 接口草图仍有效，冲突以 rev.8 为准）

复用 evmone::state（vcpkg overlay port，REF 3585c2cb = evmone 0.21.0 + SM3）的 **ETH + OP 统一 evmone 执行底座**：
`eth/` 为 OpStack 与纯 ETH 的共享内核，`opstack/` 在其之上实现 OP 薄层。
生产编排仍留 `bcos-evm/opstack/`；与现有 `bcos-evm/` 严格隔离（互不 include / 不链接）。

## 当前阶段（rev.8.2）

| 里程碑 | 状态 |
|--------|------|
| M0 overlay 导出 / M1 writeback / M2 EEST state / M3 EEST blockchain / M3.5 P1 读放大 spike | ✅ 完成（见下方验收记录） |
| M4 OP 数据层（`OpForkSchedule`/`OpPredeploys`/`OpPrecompiles`/`OpFeeParams`/`OpDepositTx` + 向量 schema） | ✅ 完成（11 单测 + `docs/vector-schema.md`） |
| M5 OP 执行层（`OpHost`/`opValidate`/`opTransition`/`runDeposit`/`RollupCost` + 块级 harness） | ✅ 完成（32 单测含 §4.4 冒烟） |
| M6 零值差分护栏 | ✅ 部分完成（`OpZeroDiff`：非 vault 账户 ≡ ETH；BaseFeeVault = gasUsed×baseFee；upstream diff 脚本仍待） |
| M3.5 P2 真账本桥接 / E-b（ref t8n gate + 生产切内核） | 🅿️ **park**（E-b 解冻前不得宣称 OP 路径生产可用 / op-geth 等价，见 spec §1.1 R2） |

### M6 零值差分口径

`OpFeeParams=0`、`has_operator_fee=false`、关闭 precompile override 时，同一笔普通转账：

- `status` / `gas_used` 与 `eth::runTransaction` 相等
- **非** BaseFee/L1/Operator vault 的 `state_diff`（含 coinbase=SequencerFeeVault tip）逐位相等
- OP 侧 `BaseFeeVault.balance == gasUsed × baseFee`（ETH 隐式销毁 → OP 显式入账）
- 不验证 L1/operator fee 本身；t8n 仍属 E-b

## Naming

| 类别 | 风格 | 例 |
|------|------|-----|
| 类 / 结构体 | PascalCase | `OpHost`、`DepositTx`、`OpFeeParams` |
| 自由函数 / 方法 | camelCase | `runTransaction`、`opValidate`、`opTransition`、`runDeposit`、`applyStateDiff` |
| 结构体字段 | snake_case | `l1_base_fee`、`gas_limit`、`has_operator_fee` |
| 成员变量 | `m_` + snake_case | `m_chain_id` |
| 常量 | `OP_*` / `kCamel` | `OP_L1_BLOCK`、`kL1CostIntercept` |

例外：从 evmone 逐行照抄的匿名 ns 助手（如 `build_message`、`process_authorization_list`）保留母本 snake_case，不改名。

## Build (standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build

## Build (in-tree, as part of FISCO-BCOS root build)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DBCOS_EVM_REF=ON
    cmake --build build --target bcos-evm-ref-eth

`BCOS_EVM_REF`（根 `CMakeLists.txt`，默认 `OFF`）挂接本模块的 `add_subdirectory`；关闭时对现有构建零影响。

## Test

    export EVM_REF_EEST_ROOT=<EEST fixtures root>   # 未设置则 EEST 用例 SKIP
    ctest --test-dir build --output-on-failure

## M2 验收记录

- EEST release: 见 `test/EEST_VERSION`（v5.4.0, `fixtures_develop.tar.gz`；与 evmone REF 3585c2cb 配对；Task 5 Step 1 选定）
- state fixtures（harness 日志原样）：`files=2723 skipped=0 failed_files=0`
  （耗时约 17–19s；`ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests` 全绿）
  - 2723 个 fixture 文件覆盖全部 fork 子目录；其中 **2717 个文件含 Cancun+ case**
    （harness 内部按 `rev >= EVMC_CANCUN` 过滤，非 Cancun+ 的旧 fork 文件被遍历计数但不产生断言）
  - **Cancun+ case 总数 55,233，全部通过**（63,556 全 fork case 中）
- skip 清单（`EVM_REF_EEST_SKIP`）：无（本次运行未设置该变量，0 skip）
- 数字来源：Task 5 审查者独立复测（详见 `.superpowers/sdd/task-5-report.md` 控制器更正节）

## M3 验收记录（EEST blockchain）

- EEST release: 同 `test/EEST_VERSION`（v5.4.0）
- blockchain fixtures: 2848 files, failed_files=0, unsupported_files=2（cancun/eip4844 无效 RLP 块，属 spec §1.3 范围外）, ~61s
- `EestBlockchain.Smoke`（cancun/ 前 20 文件）进 ctest；`EestBlockchain.Full` 由 `EVM_REF_EEST_BLOCKCHAIN_FULL=1` 门控（3.2 GB，夜跑级）
- 判据经两组定点变异测试证伪假绿（破坏 stateRoot / receiptTrie 各一次，均按预期 FAIL 并精确定位）

**重要说明（spec §7.0 rev.5）**：本模块 0 失败**不构成 parity gap 证据**。`bcos-evm` 在同一批 fixture 的
Cancun+ 区间上同样干净，其 405 个失败 100% 落在 pre-Cancun（404 `fork_Frontier` + 1 `fork_Homestead`）。

## M3.5 Phase 1（读放大 spike）

见 `spike/README.md`。判定 GO：粗粒度 `get_account` 放大 1.16x；最大浪费是上游负查询不缓存（占全部
账本读 27.9%），适配器侧 5 行可修。
