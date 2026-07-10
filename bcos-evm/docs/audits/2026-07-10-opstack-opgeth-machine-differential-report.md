# opstack op-geth 机器差分报告（M-T t8n gate）

**日期：** 2026-07-10
**Plan：** [`docs/superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md`](../superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md)（v2，Task 1–5 全部 complete）
**台账：** [`bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`](../../test/opstack/t8n/vectors/DIVERGENCES.md)
**生成器：** [`bcos-evm/test/opstack/t8n/generator/README.md`](../../test/opstack/t8n/generator/README.md)
**op-geth pin：** `v1.101702.2` @ `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`（与 `bcos-evm/test/eth-eest-test/assets/upstream-pins.json` 一致）
**关联的 7 份既有人工文档（同目录，其中 6 份为 parity 审计，`*-work-list.md` 为工作清单）：** `2026-06-20-opstack-isthmus-audit.md`、`2026-06-20-opstack-isthmus-work-list.md`、`2026-06-21-l2-tx-rlp-to-receipt-comparison.md`、`2026-06-21-opstack-isthmus-reaudit-wave3.md`、`2026-07-01-opstack-vs-op-geth-parity-phase0-2.md`、`2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md`、`2026-07-01-opstack-vs-op-geth-parity-validation.md`

---

## 0. 一句话结论

50 条向量、**0 条未入账分歧**、回放二进制 `exit 0`——但这**不是全绿**：32 条向量字段被机器打成 `KNOWN-DIVERGE`，全部归并为 **2 个 CONFIRMED、共识级的 `bcos-evm/opstack` 缺陷**，都是此前 6 份人工 parity 审计未发现的。spec §7.0 把这个 gate 称为"唯一能为替换论证提供*正确性*证据的实验"；本报告就是它的产出——它没有证明 opstack 与 op-geth 等价，它证明了**人工审计的盲区是真实的**。

---

## 1. 方法

### 1.1 为什么不能直接用 op-geth 的 `evm t8n`

op-geth v1.101702.2 的 `t8ntool` 不支持 OP：fork 注册表（`tests/init.go`）只到 Prague/Osaka/Verkle，没有 Ecotone/Isthmus/Jovian；无 0x7E deposit tx 解析路径。optimism monorepo 本地检出也没有现成的 OP 版 t8n（详见 plan 的"生态方案检验记录"：其 `op-test-vectors/` 现存 14 条向量经实证检验，4 条 L1-fee receipt 的期望值与自带交易不匹配、全部 14 条不可再生——不得作为差分判据采信）。

### 1.2 opt8n：把 op-geth 当库用

`bcos-evm/test/opstack/t8n/generator/main.go`（~300 行 Go，仅离线运行、不进 CI 构建）把 op-geth v1.101702.2 当库导入，执行循环**照抄**其自己的 `cmd/evm/internal/t8ntool/execution.go`（`(*Prestate).Apply`：StateDB 构造、逐 tx `core.ApplyTransactionWithEVM`、receipt 收集、`statedb.Commit`+diff），只替换两处：

1. **chainConfig 装配**：以上游已有的 `params.OptimismTestConfig` 为底（`--fork isthmus` 时把 `JovianTime` 置 `nil` 关闭；`--fork jovian` 保留其默认的 `JovianTime=0`）——不是手工重新拼一份 `ChainConfig`，字段名先用 `grep -rn "IsthmusTime\|RegolithTime\|Optimism" params/config.go` 核实过并记录在生成器 README。
2. **tx 解析 + L1Cost/OperatorCost 装配**：`_op_type: "deposit"` 走手工构造的 `types.DepositTx`；普通 tx 走 `_op_raw`（`tx.UnmarshalBinary`）或字段+`secretKey`（生成器代签，产出 `_op_raw` 写回向量）。`vmContext.L1CostFunc`/`OperatorCostFunc` 按 `core/evm.go`'s `NewEVMBlockContext` 的同一模式装配（vanilla `t8ntool` 从不设置这两个字段——plain `ethereum/tests` 向量没有 L1Block 预置）。

生成器**从不接受** `expected`/`postState` 作为输入（`inputVector` 类型没有这些字段）——这是预注册纪律第 2 条（"期望值只能来自生成器输出"）的机械化实现，不是靠人自律。

### 1.3 回放器：CI 纯 C++

`bcos-evm/test/opstack/T8nVectorReplayTest.cpp`（Boost.Test）遍历 `t8n/vectors/*.json`（跳过 `*.in.json`），逐向量：`InMemoryStateView` 播种 `pre` → 逐 tx 构造消息（普通 tx 走 `_op_raw` 权威 RLP 解码 + sender 恢复；deposit 走字段；`_op_type: "setcode"` 走字段形式，理由见 §5）→ `task::syncWait(applyOpStackMessage(...))`（与既有 opstack 测试同一入口，模式照抄 `DepositMintTest.cpp`）→ 逐字段 diff `postState`/`receipts`/`blockGasUsed` against `_op_expected`。CI **不需要 Go**——生成器只在向量重生成时离线运行一次；回放器是独立的 `add_executable` + `add_test`，与其余 ~10 个 opstack 测试目标同层（`bcos-evm/test/cmake/OpStackTests.cmake`）。

### 1.4 预注册纪律（先于任何向量生成写死，Task 5 未变更）

1. **分歧即 finding，不许静默 skip**：任何 `(vectorId, field)` 不匹配必须三选一归因——(a) `bcos-evm/opstack` 缺陷（开 issue，台账 `status=PENDING-FIX`）/ (b) 生成器缺陷（修复后整批重生成，向量库不留痕迹）/ (c) 已知可接受差异（**须用户逐条签核**为 `SIGNED-OFF` 才能豁免，gate 从不自我授权）。
2. **期望值只能来自生成器输出**，`inputVector` 类型结构上不含 `expected`/`postState` 字段，手改无效。
3. 生成器与向量**同 commit 入库**；重生成必须整批。

---

## 2. CI 接线（本任务 Task 5 交付，已验证）

`OpStackT8nVectorReplayTest` 已在 `bcos-evm/test/cmake/OpStackTests.cmake` 注册为独立 `add_test`，进默认 ctest（无特殊标签、无需额外 `-D` 开关）：

```
$ ctest --test-dir build-bcos-evm-check -R T8n
Test project .../build-bcos-evm-check
    Start 274: OpStackT8nVectorReplay
1/1 Test #274: OpStackT8nVectorReplay ...........   Passed    1.45 sec
100% tests passed, 0 tests failed out of 1
```

**"绿得无声"的修复**：`KNOWN-DIVERGE`（已入账的已知分歧）经 `BOOST_WARN_MESSAGE` 打印，而 Boost.Test 默认 `log_level=error` 会吞掉 warning 级输出——CI 若不改这一行，日志将看不出这条 ctest 背后其实拖着 32 条已知共识级缺陷。Task 5 在 `add_test` 里加了 `--log_level=warning`：

```cmake
# --log_level=warning: KNOWN-DIVERGE lines (DivergenceLedger-exempted mismatches, see
# vectors/DIVERGENCES.md) are emitted via BOOST_WARN, which Boost.Test's default log_level
# (error) suppresses. Without this flag the gate would go "silently green" in CI even while
# carrying 32 allowlisted divergences (FINDING-1/FINDING-2) -- the flag makes them visible in
# every CI run's log without turning them into failures.
add_test(
    NAME OpStackT8nVectorReplay
    COMMAND ${OPSTACK_T8N_VECTOR_REPLAY_TEST_BINARY_NAME} --log_level=warning
)
```

直接跑二进制核实（本次复跑，非估算）：

```
$ ./bcos-evm/test/OpStackT8nVectorReplayTest --log_level=warning
...
warning: in "replay_t8n_vectors": KNOWN-DIVERGE jovian_7702_authorize_and_call FINDING-2 field=receipts[0].gasUsed want=0xd936 got=0x10a0a
...
*** No errors detected
$ echo $?
0
```

- `exit 0`：无 `BOOST_CHECK`/`DIVERGE`（红）失败——0 条未入账分歧。
- `KNOWN-DIVERGE` 行计数：**32**（`grep -c KNOWN-DIVERGE` 输出）。
- 按 finding 拆分：`FINDING-1` × **8**、`FINDING-2` × **24**——与 `DIVERGENCES.md` 台账逐条比对一致。
- 台账 `ALLOWLIST` 行数 `grep -c "ALLOWLIST vectorId"` = 33，比 32 多 1——第 38 行是"Machine format"节的**格式示例**（`vectorId=<id> field=<field> ...`），不是真实条目；32 条真实豁免与运行时 `KNOWN-DIVERGE` 数字精确对应。

向量库文件计数（本次复核，非估算）：`t8n/vectors/` 下 `.json`（非 `.in.json`、非 `DIVERGENCES.md`）= **50**；`.in.json` 输入用例 = 51（多出的 1 个是 `deposit_system_tx_rejected.in.json`——生成器对该场景全程报错退出、从不产出对应输出文件，作为文档留档，见 §4）。

---

## 3. 覆盖矩阵

50 条向量，两个 fork（Isthmus 41 条 / Jovian 9 条）：

| 类别 | 条数 | 覆盖点 |
|---|---:|---|
| **fee 环境观测合约**（`*_observer_*`） | 12 | 手写 141 字节字节码，同一 tx 内 `SSTORE` `CHAINID`/`GASPRICE`/`COINBASE`/`BASEFEE`/`ORIGIN`/`NUMBER`/`TIMESTAMP`/`GAS`/四 vault `BALANCE`/`SELFBALANCE`/`CALLVALUE` 到 14 个固定槽——一次 `postState` 比对同时验证 14 个 opcode 的真实运行时取值，是这份矩阵里唯一"人审看代码永远想不到、只有真实执行对照才能测"的一类（详见 §6 的有效性实证） |
| **普通转账 + L1 fee**（`*_transfer_*`） | 9 | L1 attributes deposit（首笔，两 fork 布局）+ 后续 EIP-1559 转账；fee 三/四 vault 入账；sender 守恒式（Task 1 首向量做过一次手算全等式核对，此后不再人审） |
| **用户 deposit**（`*_deposit_*`） | 9 | mint≠value、mint=0、成功、EVM revert（`gasUsed`=实际）、处理级失败（`gasUsed`=`gasLimit`、nonce 仍 bump）、`to=null` 创建、大 calldata（2000B 全非零，两 fork） |
| **operator fee**（`*_operator_fee_*`） | 8 | 常量项/scalar 项独立验证、余额不足、大 gap、zero/zero 边界、Isthmus/Jovian 公式差异（`gas*scalar*100+constant`） |
| **EIP-7702 × OP**（`*_7702_*`） | 6 | 授权+同 tx 委托调用、单纯授权消费 nonce（不执行委托代码）、`extcodesize`/`extcodehash` 观测、delegated sender 转账、nonce 不匹配时授权被忽略 |
| **边界**（`*_boundary_*`） | 5 | intrinsic gas 不足被拒、EIP-7623 floor 抬高 `gasLimit` 门槛被拒、floor 抬高实际 `gasUsed`（成功但计价更高）、余额恰好 1 wei 付不起 l1Cost 被拒、余额恰好归零（成功边界） |
| **wave-0 种子**（`*_seed_*`） | 1 | post-Canyon deposit receipt 语义（`depositNonce`/`receiptVersion=1`）——`isthmus_transfer_basic` 承接了 Task 1 首向量角色 |

**4 个已记录的不可达场景**（未产出可执行向量，全部记于 `DIVERGENCES.md`"Non-divergence known limitations" + `generator/README.md`"Known limitations"，不计入上表 50 条，也不带任何归因，因为回放从未运行）：

1. **`is_system_tx=true` post-Regolith deposit**：op-geth `preCheck()` 对该组合返回 `ErrSystemTxNotSupported`，作为 Go error 中止 `opt8n` 整个向量文件生成（不是产出一个"被拒绝"的向量）——实测报错文本：`opt8n: vector "isthmus_deposit_system_tx_rejected": tx 0: ApplyTransactionWithEVM: system tx not supported: address 0xdeadDEADdeAddeadDeadDeadDeaDDEADdead0011`。`deposit_system_tx_rejected.in.json` 保留在向量目录仅作文档。
2. **Pre-Canyon deposit receipt 语义**（`DepositReceiptVersion=nil`）：`opt8n --fork isthmus|jovian` 只提供两个固定的、全 post-Canyon 的 chainConfig，无按向量调 Canyon-time 的旋钮——按 plan 自身的许可语言列为范围外。
3. **L1Block calldata 解析的 ground truth 不可获得**：生成器从不部署真实 `L1Block.sol` 字节码（值直接预置进 `pre`），对无代码地址的 `CALL` 在真实 EVM 语义下是纯 no-op——op-geth 对"deposit calldata 如何改写 L1Block 存储"这件事**永远没有 ground truth**，而 `bcos-evm/opstack` 原生 dispatch 会真的解析并写槽。这个矩阵格无法被这套生成器设计差分测试；需要另一套 harness（手算 oracle 或把编译后的 `L1Block.sol` 字节码接入生成器 `pre`）。
4. **blob tx（type 0x03）拒绝**（Task 4 边界行原计划的一项）：生成器的两个固定 chainConfig 都是 post-Cancun，vanilla L1 op-geth 在 Cancun+ **接受** blob tx——这项拒绝是纯 OP-Stack 政策决定（L2 无 L1 blob 可用性），op-geth-as-library 完全没有对应的 ground truth 可差分。已由专用单测覆盖（`bcos-evm/test/opstack/OpStackPreCheck4844Test.cpp`）。

以上 4 项均实证核实为生成器设计的结构性边界（"op-geth 当库、L1Block 值预置而非部署字节码、固定 `--fork` chainConfig"），不是偷懒也不是 opstack/生成器缺陷。

---

## 4. 两个 CONFIRMED finding

两者都是**共识级**——影响 `receipts_root`/`state_root`，不是显示层小问题。**Gate 只报告，不修复**（预注册纪律第 1 条 + plan Task 3/4 的明文规定"不修 opstack（FINDING 的修复另立 plan）")；两者的修复方向已记录，供后续独立 plan 使用。

### FINDING-1（CONFIRMED，归因 a——deposit 漏 EIP-7623 calldata floor）

**位置**：`bcos-evm/opstack/settlement/OpStackTxFinalize.cpp:45-50`

```cpp
void applyDepositPostExecuteSettlement(
    StateTransitionContext const& ctx, OpStackTxFinalizeResult& out)
{
    // Deposits have no Regolith floorDataGas charge.
    applyPostExecuteSettlement(ctx, 0, out);
}
```

对**全部** deposit 交易（成功或 revert）硬编码 `floorDataGas=0`。该注释混淆了 op-geth 的两个不同时代：pre-Regolith deposit 确实无条件报告 `gasUsed=gasLimit`（或系统 tx 为 `0`），与任何 floor 无关（`core/state_transition.go:625-642`，由 `!rules.IsOptimismRegolith` 守卫）；但 EIP-7623 的 floor-adjustment 代码块（`core/state_transition.go:644-662`，`if rules.IsPrague { if st.gasUsed() < floorDataGas { ... } }`）一旦 Prague-等价规则激活（Isthmus 满足）就**无条件**对每笔交易运行，不区分是否 deposit；post-Regolith 的 deposit（`core/state_transition.go:681-689`）返回的是**已经过 floor 调整**的 `st.gasUsed()`。故真实 op-geth（pinned v1.101702.2）**确实**对 post-Regolith（即 Isthmus/Jovian）deposit 的 `gasUsed` 施加 calldata floor（`tokens(data)*10`，tokens = 4×非零字节 + 1×零字节）；`bcos-evm/opstack` 对任何 floor 成本超过其正常 intrinsic+执行成本的 deposit 都没有。

**数字**（`isthmus_transfer_basic` 的首个 deposit，实证）：176 字节 calldata（18 非零 + 158 零）→ floor=23300 vs opstack 实算 intrinsic=21920，差 1380 gas。`isthmus_deposit_large_calldata`/`jovian_deposit_large_calldata`（2000 字节全非零）把差值放大到 **48000 gas**——floor 公式对非零字节 4 倍权重的印证（~11 倍字节量产生 ~35 倍差值）。Isthmus 与 Jovian 两 fork 表现完全一致（预期，因为 `0` 字面量不查 fork schedule）。

**后果**：数据重的 deposit，其 receipt `gasUsed`/`cumulativeGasUsed`/`blockGasUsed` 系统性偏低——receipts-root 级共识分歧。

**修复方向**（记录供后续 plan，本 gate 未应用）：`finalizeDeposit` 的调用点已经为入口侧 precheck 算出了 `sidecar.floorDataGas`（`OpStackFloorGasPrecheck.cpp:26`）；`applyDepositPostExecuteSettlement` 应改传这个已算好的值，而不是字面量 `0`。

**首次发现**：Task 2（`isthmus_transfer_basic`）；Task 3 波次一在另外 3 条向量上放大确认（同一根因，合并进本条，未拆分为独立 finding，遵 plan 明文指示）。

**与既有人工审计的对照**：6 份人工 parity 审计中 `2026-07-01-opstack-vs-op-geth-parity-validation.md` 的 D8 项记录了"floor gas **时机**"（`buyGas→floor→intrinsic` 顺序）大体一致、仅错误码映射有 🟡 差异——但没有指出 deposit 路径**完全不参与** floor 计算这个具体的数值级缺陷。这正是"审计预测了类别、机器 gate 在生产模块抓到实例"的例子：`bcos-evm-ref` 第二轮 spec 审查（spec §4.3）独立预测过"Isthmus 下 7623 floor 适用 deposit 无豁免"这一类问题，本 finding 是该预测在生产 `opstack/` 代码里的机器实证。

**Issue 文本（草稿，供开 issue 使用）**：

> **Title**: `opstack` deposit 交易在 Isthmus+ 下未应用 EIP-7623 calldata floor（`OpStackTxFinalize.cpp:45-50`）
>
> `applyDepositPostExecuteSettlement` 对全部 deposit 硬编码 `floorDataGas=0`，导致 Isthmus+（Prague-等价规则）下数据较重的 deposit 交易的 `gasUsed`/`blockGasUsed` 系统性低于 op-geth v1.101702.2 的实际计算值——receipts-root 级共识分歧。根因：注释把 op-geth "pre-Regolith deposit 无条件 gasUsed=gasLimit"误读为"post-Regolith deposit 也豁免 EIP-7623 floor"，但 op-geth `state_transition.go:644-662` 的 floor 调整对所有交易（含 deposit）无条件运行。修复：`applyDepositPostExecuteSettlement` 应改用调用点已算出的 `sidecar.floorDataGas`（`OpStackFloorGasPrecheck.cpp:26`），而非字面量 `0`。经 M-T t8n gate 机器验证（`isthmus_transfer_basic`/`isthmus_transfer_multi_nonce`/`isthmus_deposit_large_calldata`/`jovian_deposit_large_calldata` 共 8 条向量字段，见 `DIVERGENCES.md` FINDING-1）。

### FINDING-2（CONFIRMED，归因 a——OP 结算路径读错 refund 来源，丢失 EIP-7702 授权 refund）

**位置**：`bcos-evm/opstack/settlement/OpStackTxFinalize.cpp:28-34`（`applyPostExecuteSettlement`）

```cpp
auto const stateRefund =
    gas::isEip1559GasRefundEnabled(ctx.revisionConfig) ?
        static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
        uint64_t{0};
```

EIP-3529 封顶 refund 的来源取自 **`ctx.evmcResult.gas_refund`**——evmone 自身的 SSTORE-clear refund 计数器，随原始 EVMC 结果一并返回。这个计数器**看不到** host 侧（`bcos-evm` 自己的 `State::add_refund()`）追加的 refund，尤其是 EIP-7702 授权处理产生的 refund（`bcos-evm/eth/eip/Eip7702.cpp:applyAuthorizations`：`state.add_refund(PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST)` = `state.add_refund(12500)`，对每条授权 tuple 的 authority 账户已存在的情形逐条记入，这是首次委托一个既有 EOA 的常见情形）。

**正确来源已经存在**：`ctx.evmGasRefund`（`StateTransitionContext`），在 `bcos-evm/eth/kernel/state-transition/StateTransitionExecute.cpp:167` 赋值（`ctx.evmGasRefund = kernelOutput.gasRefund;`，源自 EVM 退出时的 `state.get_refund()`）——这正是 `GasSettlementTypes.h:103-104` 自己的文档注释点名的权威字段（"state.get_refund() at EVM exit (authoritative over evmc gas_refund)"）。`OpStackTxFinalize.cpp` 从未读取 `ctx.evmGasRefund`，而是独立地从错误的（evmc 原生）字段重新推导 `stateRefund`。

**同库内部不一致**（坐实这是一处遗漏而非设计选择）：ETH 路径 `PostExecuteGasMetering.h:54/59` 用的是 `snapshot.evmGasRefund`（正确）；OP 路径用的是 `evmcResult.gas_refund`（错误）——两条路径本应共享同一权威来源。

**数字**（实证，经 EIP-3529 封顶公式验证）：`isthmus_7702_authorization_nonce_consumed`/`isthmus_7702_extcodesize_extcodehash` 差值 = 9200 = `min(12500, 46000/5)`；`isthmus_7702_authorize_and_call`/`jovian_7702_authorize_and_call` 差值 = 12500（未触顶）。回放器调试跟踪也直接观察到 `ctx.evmGasRefund`（内部日志字段 `gasRefund=12500`）算对了，但 `applyOpStackMessage` 最终产出的 `gasUsed` 没有反映这个 refund 的扣减。

**后果**：任何在**已存在**账户上安装 EIP-7702 委托的 OP normal tx，其 `gasUsed`（以及由此派生的 `blockGasUsed`、coinbase tip、BaseFeeVault、OperatorFeeVault、sender 净扣款）系统性偏高，偏高量为 `min(12500×授权数, gasUsed峰值/5)`——receipts-root **与** state-root 级共识分歧。`finalizeDeposit` 复用同一 `applyPostExecuteSettlement`（经 `applyDepositPostExecuteSettlement`），故携带授权列表的 deposit tx 理论上有同样的 bug——本波次向量矩阵未构造这一组合（deposit + 7702 授权列表），但代码路径是共享的，风险同样存在，记录于此供后续覆盖。

**首次发现**：Task 4（`isthmus_7702_authorization_nonce_consumed`——最简单的一条，单授权、无同 tx 委托代码执行，把 bug 隔离到纯授权 refund 记账本身）。4 条受影响向量横跨 Isthmus/Jovian、有/无同 tx 委托代码执行，均一致；`isthmus_7702_delegated_sender_transfer`（委托 EOA 发起普通转账、无 setcode tx）与 `isthmus_7702_authorization_invalid_nonce_ignored`（nonce 不匹配、`add_refund` 从未被调用）两条向量均绿——与"根因专属于授权 refund 记账"一致。

**修复方向**（记录供后续 plan，本 gate 未应用）：`applyPostExecuteSettlement` 的 `stateRefund` 改读 `ctx.evmGasRefund`，而非 `ctx.evmcResult.gas_refund`。

**Issue 文本（草稿，供开 issue 使用）**：

> **Title**: `opstack` 结算路径从错误字段读取 gas refund，丢失 EIP-7702 授权 refund（`OpStackTxFinalize.cpp:28-34`）
>
> `applyPostExecuteSettlement` 的 EIP-3529 封顶 refund 取自 `ctx.evmcResult.gas_refund`（evmone 原生 SSTORE-clear 计数器），而非权威的 `ctx.evmGasRefund`（`GasSettlementTypes.h:103-104` 自述"authoritative over evmc gas_refund"，源自 `state.get_refund()`）。`evmcResult.gas_refund` 看不到 host 侧 `State::add_refund()` 记入的 refund，尤其是 EIP-7702 每授权 tuple 的 12500 refund（`Eip7702.cpp::applyAuthorizations`）。同库 ETH 路径（`PostExecuteGasMetering.h:54/59`）已经正确使用 `snapshot.evmGasRefund`——OP 路径应对齐。后果：任何在已存在账户上安装 EIP-7702 委托的 OP normal tx（deposit tx 携带授权列表理论上同样受影响，本次向量矩阵未覆盖该组合）的 `gasUsed` 及全部派生费用偏高 `min(12500×授权数, gasUsed/5)`——receipts-root/state-root 级共识分歧。修复：`applyPostExecuteSettlement` 改读 `ctx.evmGasRefund`。经 M-T t8n gate 机器验证（`isthmus_7702_authorization_nonce_consumed`/`isthmus_7702_authorize_and_call`/`isthmus_7702_extcodesize_extcodehash`/`jovian_7702_authorize_and_call` 共 24 条向量字段，见 `DIVERGENCES.md` FINDING-2）。

---

## 5. 范围限制（非分歧，工程决策，不影响两个 finding 的有效性）

- **EIP-7702 向量用字段形式，非 `_op_raw` 权威解码**：`bcos-rpc` 的 `Web3Transaction` RLP 编解码（`TransactionType` 枚举止于 `EIP4844=3`）没有 type-0x04（setcode）解码路径；扩展它需要改 `bcos-rpc`，超出 spec rev.7 D3 对本 gate"仅限 `bcos-evm`"的授权范围。授权 tuple 本身（chainId/address/nonce/yParity/r/s）仍是 op-geth 真实签名产出，由 `bcos-evm/opstack` 自己的 `recoverAuthorizationAuthority`（`eth/eip/Eip7702.cpp`）做真实 secp256k1 恢复——这部分端到端仍是真实执行对照，只有外层 tx 的 sender 取自生成器 `_op_from`（本身也是真实签名密钥的 `crypto.PubkeyToAddress`）。
- **blob tx 拒绝无 op-geth ground truth 可差分**（见 §3 第 4 项不可达场景）——已由专用单测覆盖。
- **无 BLOCKHASH 探测向量**：Task 1 审查裁定生成器的 `GetHash` 返回零 hash 是简化实现，Task 3/4 据此不得引入依赖真实历史块哈希的观测。

---

## 6. gate 有效性实证

如果这个 gate 只是走个过场，观测合约向量应该也全绿——但它们真的能测到东西，且测到的地方确实是对的：观测合约（141 字节手写字节码）真实覆盖了 `CHAINID`（slot0）、deposit 上下文的 `GASPRICE`（slot1，验证 deposit tx 执行期间 `ctx.gasPrice` 确实保持默认零值）、四个 vault 的 `BALANCE`（slot8–11）——这 3 处**全部回放为绿**，即 opstack 在这些点上是对的。特别是 `spec §4.3` 曾警告的"vault 不得在执行前 credit"，在 `*_observer_normal_*` 系列向量里被机器验证通过（预置的 marker 余额原样读回，而不是结算后的金额，证明 op-geth 与 `bcos-evm/opstack` 都把 vault 入账推迟到 EVM 执行**之后**）。

换句话说：这个 gate 既没有"全绿糊弄"，也没有"全红报警"——它精确地把 opstack 分成了"这 3 处真的对"和"这 2 处真的错"两半，而这正是差分测试该有的行为。

---

## 7. 对 spec §7.2 的意义

spec §7.0 把这次 gate 定性为"唯一能为『替换』论证提供*正确性*证据的实验，且无论终局如何都是纯增益"。本报告的产出印证了这句话，但方式和 plan 起草时预想的两个分支都不完全一样：

- **不是"0 分歧，opstack 的 op-geth 等价性首次获得机器强度确认"**——有 2 个 CONFIRMED 共识级缺陷。
- **也不是"opstack 不可用"**——32 条已知分歧全部归并到 2 个根因（不是 32 个独立 bug），且都是窄范围的可修复缺陷（一处硬编码 0、一处读错字段），观测合约向量证明的另外 3 处（CHAINID/deposit-GASPRICE/四 vault 余额时机）是真的对的。

**这次 gate 首次为 `bcos-evm/opstack` 的 op-geth 一致性提供了机器强度的证据——证据显示：既有大部分正确，也有具体、可定位、可修复的错误。**

**比"未发现"更尖锐的事实（终审实证补记，2026-07-10）**：人工审计不只是漏掉了这两个缺陷，还曾把缺陷所在区域**逐条标绿**——
- `2026-06-21-opstack-isthmus-reaudit-wave3.md:139` 把 "existence refund 12500" 对照 `Eip7702.cpp` 标为 ✅：它核实了 refund 被正确**记入**，但没有核实它是否被正确**读出**用于结算（FINDING-2 恰在读出侧）；
- `2026-06-21-l2-tx-rlp-to-receipt-comparison.md:160` 把 "refund cap 1/5 peak" 标为 ✅：它核实了封顶**公式**正确，但没有核实封顶的**输入来自哪个字段**（同样是 FINDING-2）。

这不是审计者的疏忽，而是**方法的固有边界**：逐条对照代码能验证"某个语义点被实现了"，无法验证"这个实现在完整执行路径上被正确接线"。两个 CONFIRMED finding 都发生在**接线处**（deposit 分支绕开 floor；OP 结算读了另一个字段），而非语义实现处——这类缺陷只有真实执行两条独立实现、逐字段比对数值才能暴露。 这正是 6 份人工 parity 审计（4,485 行代码、~85 测试文件、覆盖到 Jovian）**没有做到**的：它们用阅读代码 + 逐条对照 op-geth 源码的方式核实了大量语义点（部署时机、fork 覆盖、deposit 失败路径的 nonce 语义等），其中 D8 项甚至记录过"floor gas 时机"的部分观察——但没有一份指出 deposit 路径完全不参与 floor **数值**计算，也没有一份指出 refund 结算读错了字段。这两个缺陷都需要**真实执行两条独立实现、逐字段比对数值**才能暴露，人工代码阅读做不到。

对"替换 vs oracle"决策（§7.2 的核心问题）的输入价值：这次 gate 只验证了 `bcos-evm/opstack`（现有生产模块），不涉及 `bcos-evm-ref`（评估中的 evmone 替换候选，M4/M5 已被 rev.7 D1 永久取消，不实现 OP 语义）；它回答的是一个更早、更基础的问题——**现有 OP 语义本身有多准**。答案是"总体扎实、有两处具体缺陷"，而不是"完全正确"或"大面积错误"。这份结果本身不替用户裁定任何后续动作（是否开 issue 修复、修复的优先级、是否影响 §7.2 决策点的时间线），只提供此前不存在的机器强度事实。

---

## 8. 局限（如实声明）

- **内存 fixture，非真实账本**：`InMemoryStateView` 播种 `pre`，不经过协程账本/`LedgerStateView`；这个 gate 验证的是"给定同样的 `pre`，两套执行引擎产出同样的 `post`"，不测桥接层。
- **不覆盖**：blob tx 拒绝、pre-Canyon deposit 语义、`is_system_tx=true` 场景（均为生成器设计的结构性边界，见 §3）。
- **无 BLOCKHASH 探测**（生成器 `GetHash` 返回零 hash 的简化，Task 1 审查裁定）。
- **EIP-7702 用字段形式而非 `_op_raw` 权威解码**（`bcos-rpc` 无 type-0x04 解码路径，扩展超出本 plan 授权范围，见 §5）。
- **单区块、无 rejectedTxs 回滚历史**（生成器 Task 1 的已知简化，Task 4 已为"边界"类向量补上 `_op_expect_rejected` 机制，但仍是单区块内单笔判定，非完整 gas-pool 回滚序列）。

---

## 9. 结语

- 50 条向量、0 未入账分歧、二进制 `exit 0`——ctest 绿，但绿的意义已在 §2 的 `--log_level=warning` 修复后于 CI 日志中如实可见：32 条 `KNOWN-DIVERGE`，归并为 2 个 CONFIRMED 缺陷。
- 两个 finding 的修复**均不在本任务范围**（预注册纪律 + plan Task 3/4 的明文规定），issue 文本已备好（§4），供后续独立 plan 使用。
- spec `§7.1` 的 M-T 行已标记完成，链接本报告。
