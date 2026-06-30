# bcos-evm/eth/ ↔ go-ethereum EVM 差异对比

> 对比日期: 2026-06-25  
> A 方: bcos-evm/eth/ (C++ / evmone)  
> B 方: go-ethereum core/vm/ + core/state_transition.go (Go 自研解释器)

---

## 1. EVM 执行入口 & 消息模型

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| 入口函数 | `executeMessage(ExecuteMessageInput)` → `TxExecutionRunner::run()` | `ApplyMessage` / `TransitionDb` → `evm.Call/Create` | bcos-evm 多一层管线抽象 (TxPipelineContext→ExecuteMessageInput) | 低 |
| 帧模型 | `runExecutionFrame(FrameExecutionEnv, msg, FrameScope, host)` — 统一帧执行，scope=TopLevel/Nested | `evm.interpreter.Run` 递归，`evm.depth` 跟踪 | 显式 scope vs 隐式 depth，语义等价 | 低 |
| 6 种调用分派 | 无显式 switch — `isCreateKind()` 二分，CALL/DELEGATECALL 等差异由 evmone 内部处理 | `evm.Call/CallCode/DelegateCall/StaticCall/Create/Create2` 独立方法 | bcos-evm 依赖 evmone 实现调用类型语义差异 | 低 |
| evmc_message.kind 映射 | 直接使用 evmc_call_kind 枚举 (EVMC_CALL=0, EVMC_CREATE=1, …) | `vm.CallType` 枚举 → invoke 前设 msg kind | 6 种类型全覆盖 | 低 |
| 返回值 | `evmc::Result` (status_code, gas_left, output_data, create_address) + `ExecuteMessageOutput`(含 stateDiff, logs, gasRefund) | `ExecutionResult` (UsedGas, Err, ReturnData, Reverted) | 结构不同，语义对齐 | 低 |

## 2. Host 接口 (EVM ↔ 世界状态)

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| account_exists | `m_state.get_account(addr).has_value()` — 不检查空账户 | `statedb.Exist(addr)` — EIP-158 空账户 (nonce=0,balance=0,code=empty)→false | **bcos-evm 不检查 EIP-158 空账户规则，依赖外部清理** | **中** |
| get_storage | `m_state.get_storage(addr, key)`：先查 journal，回退冷存储 | `statedb.GetState(addr, hash)`：journal 化 StateDB | 一致，不存在→零值 | 低 |
| set_storage (SSTORE) | EIP-2200/EIP-3529 退款：`applySstoreRefundEip3529()` + `classifyStorageStatus()`。`fixStorageStatus` 控制精确 EVMC 分类 vs 简化二元 | `state_object.go` + `journal.go` 实现 EIP-2200/EIP-3529 | 退款逻辑对齐。`fixStorageStatus=false` 路径缺精确 EVMC 状态分类 | **中** |
| get_balance | `toEvmC(m_state.get_balance(addr))` — 不存在→0 | `statedb.GetBalance(addr)` — 不存在→0 | 一致 | 低 |
| get_code_size | `m_state.get_code(addr).size()` — 加载完整代码取大小 | `statedb.GetCodeSize(addr)` — 直接返回大小 | bcos-evm 每次都加载完整代码，效率略低，语义等价 | 低 |
| get_code_hash | 不存在→零哈希(`bytes32{0}`)，空代码→`keccak256("")`=`0xc5d24601...` | 不存在→`keccak256("")`(早期版本)；EIP-1052 后不存在→0 | **bcos-evm 比 geth 更符合 EIP-1052** | 低 |
| selfdestruct | EIP-6780 已实现：非 wasCreatedInTx→仅转账，wasCreatedInTx→标记自毁+延迟销毁。FISCO 扩展默认禁止。返回 true=已执行，false=被拒绝/降级 | `Selfdestruct6780`：返回 true=首次注册。EIP-3529 退款已移除 | 返回值语义不同但 EIP-3529 后无意义（SELFDESTRUCT 退款已移除） | 低 |
| call (嵌套调用) | `EthHost::call(msg)` → `runExecutionFrame(Nested)` → `vm.execute()` | `evm.interpreter.Run` 递归 | 深度限制 (1024) 由 evmone 强制；宿主无额外检查 | 低 |
| get_block_hash | `m_blockHashes(number)` 回调。FISCO: 检查 number≥current 或 <0→0，**不强制 256 块窗口**（evmone 在 BLOCKHASH op 前检查） | `evm.Context.GetHash` 检查 `[current-256, current)` | evmone 在 op 层检查，常规路径无影响 | 低 |
| emit_log | `LogEntry{address, topics, data}`，topic 数量由 evmone (LOG0-4) 限制 | `Log{Address, Topics, Data}`，由操作码限制 | 编码不同 (TARS vs RLP)，语义对齐 | 低 |
| get_tx_context | 缓存 `evmc_tx_context` 含 txGasPrice, blobBaseFee, blobHashes, baseFee | `vm.TxContext{Origin, GasPrice}` | bcos-evm tx_context 更丰富 | 低 |
| access_account / access_storage | EIP-2929 预热追踪：`EVMC_ACCESS_COLD/WARM`。`eip2929` 总开关 (ADR-004) 可禁用 | Berlin+ 始终启用 EIP-2929 | **`eip2929` 总开关是设计背离** | **中** |

## 3. Gas 计算

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| Intrinsic gas | 21000 + accessList(2400/1900) + calldata(4/16) + create(32000+2*words) + auth(25000) | 同公式 | **一致** | 低 |
| EIP-1559 effective price | `min(gasFeeCap, gasTipCap + baseFee)` | `effectiveGasTip = min(gasFeeCap-baseFee, gasTipCap)`，等价 | 公式等价 | 低 |
| EIP-2929 冷/热 | COLD_SLOAD=2100, WARM_READ=100。交易进入预热：sender/recipient/coinbase/createAddr/precompiles/accessList | 同常量，同预热逻辑 | 一致 | 低 |
| EIP-4844 blob | BLOB_GAS=131072, MIN_PRICE=1, FRACTION=3338477。`calcBlobBaseFee` ≡ geth `fakeExponential` | 同常量同算法 | **一致** | 低 |
| EIP-7623 floor | tokenCount = zeroByte×1 + nonzeroByte×4, floorCost = tokenCount×10, gasUsed = max(…, floor) | 同公式 | **一致** | 低 |
| Modexp gas | 三版本: EIP-198 (pre-Berlin) / EIP-2565 (Berlin..Osaka) / **EIP-7883 (Osaka+, `2*words²*max(iter,1)`)** | EIP-198 / EIP-2565 | **bcos-evm 有 Osaka 定价(EIP-7883)，geth 可能未实现** | **中** |
| Precompile gas | 见 §5 | 见 §5 | 经典预编译一致；BLS/P256 仅 bcos-evm 有 | 低 |
| Gas refund 上限 | `min(evmGasRefund, gasUsedBeforeRefund / 5)` (EIP-3529)。SELFDESTRUCT 退款已移除。EIP-7702: 每授权退款 12500。 | 同公式 | **一致** | 低 |
| Gas 结算 | `peakGasUsed = gasLimit - clamp(gasLeft,0,gasLimit)` → `effectiveRefund = min(refund, peakGasUsed/5)` → `gasRemaining = min(gasLimit, gasLeft+refund)` → EIP-7623 floor 提升 | 同结算逻辑 | **一致** | 低 |

## 4. 状态管理 & 日志

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| Journal 机制 | `State` 类：journal 向量+checkpoint 栈。Journal 条目: AccountSnapshot (完整快照), WarmAddressInsert, WarmStorageInsert, CreateWarmPinInsert | `StateDB`：字段级 journal (balanceChange, nonceChange, storageChange, codeChange, …) | bcos-evm 用完整账户快照，geth 用字段级 | 低 |
| Checkpoint 粒度 | `State::checkpoint()`: journalSize + gasRefund + touchedAccounts 去重 | `StateDB.Snapshot()`: journal index | 语义等价 | 低 |
| Log 结构 | `LogEntry{address, topics, data}` — TARS 序列化 | `Log{Address, Topics, Data}` — RLP 编码 | 编码不同，语义对齐 | 低 |
| StateDiff | `State::build_diff()` 克隆 m_accounts (不含 transientStorage) → `applyStateDiff()` 持久化。显式 diff 模式 | 无独立 diff，写入 StateDB，`Finalise`+`Commit` 持久化 | bcos-evm 有 diff→apply 两阶段，利于并行执行 | 低 |
| Nonce 初始化 | Spurious Dragon+ → CREATE nonce=1 (`initializeCreateTargetAccount`)。嵌套 CREATE 后 sender nonce+=1。`fixNonceInit` 标志确保顶层 CREATE 后 nonce 修复 | Spurious Dragon+ → CREATE nonce=1。sender nonce 在 CREATE 前递增 | nonce 递增时机: bcos-evm post-finalize, geth pre-create。均不影响同 tx 内 NONCE op 可见性 | 低 |
| 空账户清理 | `Account` 无 isEmpty 方法。生产路径 `StateDiffApplier` 无条件写入所有账户。外部(存储/共识层)负责清理 | `StateDB.Empty(addr)` 检查 nonce=0,balance=0,code=empty。EIP-158 清理在 `Finalise` 中 | **bcos-evm 依赖外部清理空账户，可能导致状态膨胀** | **中** |

## 5. 预编译合约

| 预编译 | A gas (bcos-evm) | B gas (go-ethereum) | 差异 | 风险 |
|--------|-----------------|--------------------|------|------|
| ecrecover (0x01) | 3000，使用 `evmmax::secp256k1::ecrecover()` | 3000，使用 `secp256k1.RecoverPubkey` | 库不同，结果应对齐 | 低 |
| SHA256 (0x02) | 60 + 12 × ceil(input/32) | 同 | 一致 | 低 |
| RIPEMD160 (0x03) | 600 + 120 × ceil(input/32) | 同 | 一致 | 低 |
| identity (0x04) | 15 + 3 × ceil(input/32) | 同 | 一致 | 低 |
| modexp (0x05) | 三版本: EIP-198/EIP-2565/**EIP-7883** + **EIP-7823 字段限制(1024B)** | EIP-198/EIP-2565 | **Osaka 定价和字段限制** | **中** |
| ecAdd (0x06) | 150 (Istanbul+) / 500 | 同 | 一致 | 低 |
| ecMul (0x07) | 6000 (Istanbul+) / 40000 | 同 | 一致 | 低 |
| ecPairing (0x08) | 45000+34000×k (Istanbul+) / 100000+80000×k | 同 | 一致 | 低 |
| blake2f (0x09) | 从输入前 4 字节读轮次 | 同 | 一致 | 低 |
| point_eval (0x0a) | 50000，用 evmone `kzg_verify_proof` | 50000，用 `go-kzg` | KZG 库不同，规范对齐 | 低 |
| **BLS G1Add (0x0b)** | **375** (Prague + eip2537) | — (geth 可能不支持) | **新增** | 低 |
| **BLS G1MSM (0x0c)** | **12000 × 折扣(k) × k / 1000** | — | **新增** | 低 |
| **BLS G2Add (0x0d)** | **600** | — | **新增** | 低 |
| **BLS G2MSM (0x0e)** | **22500 × 折扣(k) × k / 1000** | — | **新增** | 低 |
| **BLS Pairing (0x0f)** | **37700 + 32600×k** | — | **新增** | 低 |
| **BLS MapG1 (0x10)** | **5500** | — | **新增** | 低 |
| **BLS MapG2 (0x11)** | **23800** | — | **新增** | 低 |
| **P256Verify (0x0100)** | **6900** (Osaka + eip7212) | — (geth 可能不支持) | **新增** | 低 |

## 6. EIP 兼容性 & Revision 配置

| EIP | A (bcos-evm) 状态 | B (go-ethereum) 状态 | 差异 | 风险 |
|-----|-------------------|---------------------|------|------|
| EIP-155 (ChainID) | `evmc_tx_context.chain_id` 提供 | `evm.ChainConfig.ChainID` | 一致 | 低 |
| EIP-1559 (Fee market) | London+ → `eip1559=true`，`resolveEffectiveGasPrice` | London fork → 完整支持 | 一致 | 低 |
| EIP-2929 (Warm access) | Berlin+ → `eip2929=true` **但有总开关 (ADR-004)** | Berlin+ → 始终启用 | **可被禁用** | **中** |
| EIP-2930 (AccessList) | 类型 0x01，gas: 2400/1900 | 同 | 一致 | 低 |
| EIP-3529 (Refund reduction) | `effectiveRefundEip3529`: min(refund, gasUsed/5) | 同 | 一致 | 低 |
| EIP-3651 (Warm COINBASE) | Shanghai+ → `eip3651=true`，coinbase 预热 | Shanghai+ → 同 | 一致 | 低 |
| EIP-3855 (PUSH0) | 由 evmone 处理 | 由 geth interpreter 处理 | 一致 (evmone) | 低 |
| EIP-3860 (Initcode limit) | INITCODE_WORD_GAS=2，大小限制由 evmone 执行 | MaxInitCodeSize=49152 | 一致 | 低 |
| EIP-4788 (BEACON_ROOT) | 由 evmone 处理，parent_beacon_block_root 通过 tx_context 提供 | 同 | 一致 | 低 |
| EIP-4844 (Blob tx) | Cancun+ → `eip4844=true`，`calcBlobBaseFee` ≡ fakeExponential | Cancun+ → 同 | **一致** | 低 |
| EIP-5656 (MCOPY) | Cancun+ → `eip5656=true`，由 evmone 处理 | 同 | 一致 | 低 |
| EIP-6780 (SELFDESTRUCT) | Cancun+ → `eip6780=true`，`wasCreatedInTx` 追踪 | 同 | 一致 | 低 |
| EIP-7516 (BLOBBASEFEE) | 由 evmone 处理，`evmc_tx_context.blob_base_fee` | 同 | 一致 | 低 |
| EIP-7702 (SetCode) | Prague+ → `eip7702=true`（版本门控 AND feature 门控），完整实现 | 同 | 一致 (双重门控可能禁用它) | **中** |
| EIP-7623 (Calldata floor) | Prague+ → `eip7623=true`（feature 门控），完整实现 | 同 | 一致 | 低 |
| EIP-7212 (P256Verify) | Osaka+ → `eip7212=true`（feature 门控） | 可能未实现 | **新增预编译** | 低 |
| EIP-7823 (Modexp 限制) | Osaka+ → `eip7823=true`（feature 门控），字段长度 ≤1024 | 可能未实现 | **新增限制** | 低 |
| EIP-7883 (Modexp 定价) | Osaka+ → `2*words²*max(iter,1)`，min 500 | 可能未实现 | **新定价公式** | **中** |
| **Revision 映射** | **双类别门控: 版本级 (自动) + feature 级 (链配置)** | 单类别: 分叉/区块号 | **FISCO 的 EIP 门控更灵活但有误配置风险** | **中** |

## 7. 交易管线 (Pipeline)

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| 管线步骤 | setupMessage → checkTxRules → checkGasAfford → debitIntrinsic → checkBalanceValue → runEvm → captureSnapshot → normalize | preCheck → buyGas → intrinsicGas → EVM → refundGas (内联) | bcos-evm 显式分步+钩子 | 低 |
| Precheck 检查项 | Eth: nonce 溢出(EIP-2681), EIP-7702 代码, fee caps, 授权规则, blob | nonce, gas, balance, signature | 检查项对齐，bcos-evm 更细粒度 | 低 |
| buyGas / refundGas | Eth 路径: 管线内 intrinsic debit。OpStack: 外部 buyGas (含 L1/operator cost) | 内联在一处 | 顺序不同语义等价 | 低 |
| 失败交易 gas | 不退还已消耗 gas: `gasUsed = gasLimit - gasLeft` | 同 | 一致 | 低 |
| Receipt 生成 | 不在 bcos-evm 中，由外部从 `ExecuteMessageOutput` 构建 | 在 StateTransition 中直接生成 | 架构差异 | 低 |
| 钩子注入点 | VmHostPolicy(6), StateTransitionHooks(7), StateTransitionErrorPolicy(4) | 无等价物 | **FISCO 独有可扩展性，但钩子可能改变 EVM 语义** | **中** |

## 8. 边界情况 & 安全性

| 对比点 | A (bcos-evm) | B (go-ethereum) | 差异 | 风险 |
|--------|-------------|-----------------|------|------|
| CALL 深度限制 (1024) | 委托给 evmone，宿主无显式检查 | `CallCreateDepth=1024`，在 `evm.Call/Create` 中检查 | evmone 强制，效果一致 | 低 |
| 63/64 gas rule | 委托给 evmone | `(gas - CallStipend) * 63 / 64` | evmone 实现对齐黄皮书 | 低 |
| CREATE/CREATE2 碰撞 | `predictLegacyCreateAddress` / `predictCreate2Address`。已有非空代码→evmone 返回失败 | `crypto.CreateAddress` / `crypto.CreateAddress2` | 一致 | 低 |
| Nonce 溢出 | EIP-2681: 拒绝 `uint64_max`。EIP-7702: `n+1 < n` 检测 | `Nonce == math.MaxUint64` 拒绝 | 一致 | 低 |
| Value 溢出 | `bcos::u256` (arbitrary precision) | `uint256.Int` | 一致 | 低 |
| 代码大小 (EIP-170) | `MAX_EVM_CODE_SIZE=24576`，在 `applyCreateCodeDepositGas` 检查 | `MaxCodeSize=24576` | 一致 | 低 |
| Initcode 大小 (EIP-3860) | 2 gas/word，大小限制 49152 由 evmone 执行 | `MaxInitCodeSize=49152` | 一致 | 低 |
| SELFDESTRUCT 重入 | EIP-6780: wasCreatedInTx→标记+延迟销毁，中间调用仍可见代码 | 同 | 一致 | 低 |
| STATICCALL 限制 | evmone `EVMC_STATIC` flag 阻止写操作 | geth `readOnly` flag | 一致 | 低 |
| DELEGATECALL 语义 | evmone 保留 sender/value，EIP-7702 委托正确路由 | 同 | 一致 | 低 |
| BlockHash 256 窗口 | 宿主不强制窗口，evmone BLOCKHASH op 前检查 | 宿主侧强制 `[current-256, current)` | evmone 检查后等效 | 低 |
| EIP-7702 REVERT | Eth 路径: REVERT→SUCCESS (委托持久化)。OpStack: 不归一化 | REVERT→SUCCESS | Eth 路径一致。OpStack 是设计选择 | 低 |
| Included-vmerr 归一化 | Eth 路径: 某些错误→SUCCESS 包含在区块中。OpStack: 不归一化 | 同 | Eth 路径一致 | 低 |

---

## 总结

### 无高风险差异
未发现导致共识失败或状态损坏的明确 bug。

### 中风险差异 (6 项)

| # | 差异 | 位置 | 说明 |
|---|------|------|------|
| 1 | **空账户存在性判定** | `EthHost::account_exists` | 不检查 EIP-158 空账户规则，依赖外部清理 |
| 2 | **`eip2929` 总开关** | `RevisionConfig.h` | Berlin+ 可禁用 EIP-2929，导致 gas 计量偏差 |
| 3 | **双类别 EIP 门控** | `RevisionConfig.h` | Feature-gated EIP 可独立禁用，误配置风险 |
| 4 | **Modexp EIP-7883** | `ModexpGas.cpp` | Osaka 定价提前实现 |
| 5 | **空账户清理** | `StateDiffApplier` | 依赖外部清理，可能导致状态膨胀 |
| 6 | **钩子语义泄漏** | `VmHostPolicy` / `StateTransitionHooks` | 钩子可改变标准 EVM 行为 |

### 架构优势

1. **分层清晰**: pipeline → frame → host 三层架构
2. **可扩展**: 3 组钩子接口 (17 个钩子方法)
3. **显式 StateDiff**: 利于并行执行和调度器集成
4. **更多预编译**: BLS12-381 (7 个), P256Verify
5. **OpStack 支持**: L2 deposit/L1 cost/operator fee
