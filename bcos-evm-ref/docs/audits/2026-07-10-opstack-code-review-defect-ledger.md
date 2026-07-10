# bcos-evm-ref/opstack 代码审查缺陷台账（D-01 – D-14）

**日期：** 2026-07-10
**范围：** `bcos-evm-ref/opstack/`（spec rev.8 追认的 M4/M5 交付物，2026-07-09 提交）
**性质：** **审计台账，只记事实，不含修法。** 修复按 spec D4 纪律另立 plan，引用本文编号（D-01 起，一经落盘不再重排；后续新发现追加 D-15+）。
**来源：** 两轮多 agent `/code-review`（high）：
- 第一轮（workflow `w8udf73d1`，14 agent，**1 个 verifier 被限流** → 覆盖不完整）：产出 D-01/D-02/D-03/D-04/D-10 及 D-14 的 build_deposit_message 重复项。
- 第二轮（workflow `w6nubzeyx`，31 agent，0 失败，指示跳过第一轮已确认项、扩展全目录覆盖）：产出 D-05–D-09、D-11–D-13 及 D-14 的 FastLZ 项。

每条均经独立对抗 verifier 判 CONFIRMED；关键前提（evmone `host.cpp:239-240` nonce 约定、`state.cpp:629-636` refund→floor 顺序、`min_gas_cost` = EIP-7623 floor）另经主对话直读 evmone 源码复核。

**对照基线：** evmone REF `3585c2cb`（= v0.21.0 tag + SM3 补丁，下引 `test/state/` 行号均以此为准）；op-geth v1.101702.2。op-geth 是 OP 语义唯一正确性基准（spec rev.8 D5：本模块与 `bcos-evm` 无任何关联，其 opstack 实现不构成对照依据）。

**严重度口径：** 🔴 共识级（同一输入下 receipt/state/块有效性与 op-geth 分歧）· 🟡 共识风险（功能未实现但当前无调用点产生实际分歧）· 🟢 清理（无语义影响）。

---

## 总览

| # | 位置 | 一句话 | 严重度 | 轮次 |
|---|------|--------|--------|------|
| D-01 | OpDepositTx.cpp:60 | 余额可支付性用 mint 之前的余额校验 | 🔴 | R1 |
| D-02 | OpDepositTx.cpp:60 | EIP-3607 sender-not-EOA 检查误用于 deposit | 🔴 | R1 |
| D-03 | OpDepositTx.cpp:80 | deposit gasUsed 不减 gas refund | 🔴 | R1 |
| D-04 | OpDepositTx.cpp:67 | GAS_LIMIT_REACHED 等块级错误降级为失败 receipt | 🔴 | R1 |
| D-05 | OpDepositTx.cpp:81 | CREATE 前不递增 nonce → 部署地址差一 / nonce=0 断言崩溃 | 🔴 | R2 |
| D-06 | OpDepositTx.cpp:47,65 | deposit receipt 类型标 legacy 而非 0x7E | 🔴 | R2 |
| D-07 | OpDepositTx.cpp:64 | deposit receipt 从不计算 logs bloom | 🔴 | R2 |
| D-08 | OpDepositTx.cpp:26 | deposit 不解析 EIP-7702 委托 | 🔴 | R2 |
| D-09 | OpDepositTx.cpp:78 | deposit 缺 EIP-2929/3651 预热序幕 | 🔴 | R2 |
| D-10 | OpForkSchedule.cpp:12 | `disable_prague_requests` 死配置，6110/7002/7251 抑制未实现 | 🟡 | R1 |
| D-11 | OpForkSchedule.cpp:38 | Granite/Holocene 丢 bn256Pairing 112687 字节输入上限 | 🔴 | R2 |
| D-12 | OpHost.cpp:93 | P256VERIFY（0x100）在 Isthmus/Jovian 不预热 | 🔴 | R2 |
| D-13 | OpValidate.cpp:41 | OpFeeParams 每 tx 从存储重读 8 次（块级常量） | 🟢 | R2 |
| D-14 | OpDepositTx.cpp:10 等 | 消息构造逐字节重复 + 同一 envelope FastLZ 压缩两遍 | 🟢 | R1+R2 |

计：🔴 × 11、🟡 × 1、🟢 × 2。🔴 中 9 条集中在 `runDeposit`。

---

## 第一组：runDeposit（D-01 – D-09）

**共同根因**：`runDeposit`（`OpDepositTx.cpp:33-100`，约 100 行）绕开 evmone 的 `transition()`（`test/state/state.cpp`）自行搭建了一条最小执行环。baseline `transition()` 里那些"看不见的"步骤——7702 委托解析、2929/3651 预热、refund 结算、nonce 先递增再执行、bloom 计算——被整体丢失。对照组：同模块普通交易路径 `OpTransition.cpp` 因照抄 baseline 结构，上述各点全部正确。测试盲区：EEST 是纯 L1 套件，没有 0x7E，deposit 路径没有任何机器差分覆盖。

### D-01 余额可支付性用 mint 之前的余额校验

- **位置**：`opstack/OpDepositTx.cpp:60-61`（mint 在 `:43-44`）
- **严重度**：🔴 共识级
- **机理**：mint 写在 `State state{view}` 的 overlay 上（`:43-44`），但 `validate_transaction(view, …)` 传入的是**原始 `view`**——余额检查看到的是铸币前余额。
- **对照**：op-geth 对 deposit 跳过 `preCheck`/`buyGas`，mint 先记账，value 转账在 `evm.Call` 内对**铸币后**余额检查（`core/state_transition.go` deposit 路径）。
- **失败场景**：标准 L1→L2 桥接 deposit（`from` 在 L2 余额 0，靠 `mint` 获得资金再转给 `to`）：`balance(0) < value` → `validate_transaction` 报 INSUFFICIENT_FUNDS → 走 `:67` 失败分支（gasUsed=gasLimit 的失败 receipt），**桥接资金送不到收款人**；op-geth 下同一笔成功。
- **验证**：CONFIRMED（R1）。

### D-02 EIP-3607 sender-not-EOA 检查误用于 deposit

- **位置**：`opstack/OpDepositTx.cpp:60-61`（与 D-01 同一 `validate_transaction` 调用）
- **严重度**：🔴 共识级
- **机理**：evmone `validate_transaction` 无条件执行 EIP-3607（sender 有 code 即拒）。
- **对照**：op-geth 对 deposit 跳过 sender-EOA 检查（deposit 不走 `preCheck`）。
- **失败场景**：sender 带 code（如 EIP-7702 委托标记的 EOA）发起 deposit：这里判 SENDER_NOT_EOA → 失败 receipt；op-geth 正常执行。
- **验证**：CONFIRMED（R1）。

### D-03 deposit gasUsed 不减 gas refund

- **位置**：`opstack/OpDepositTx.cpp:80-81`
- **严重度**：🔴 共识级
- **机理**：`gasUsed = max(dep.gas_limit - result.gas_left, p.min_gas_cost)`——`result.gas_refund` 从未参与。baseline 的正确顺序是先减 refund 再取 7623 floor（evmone `state.cpp:629-636`）。
- **对照**：op-geth 自 Regolith 起对 deposit 无条件走常规 refund 结算（`calcRefund`）；本模块普通路径 `OpTransition.cpp:198-204` 也正确地 `min(delegation_refund + result.gas_refund, refund_limit)` 后再取 floor。
- **失败场景**：任何触发 refund 的 deposit（SSTORE 清零等）：receipt gasUsed 偏高 → 块 cumulative gasUsed 偏高 → receipts-root 与 op-geth 分歧。
- **验证**：CONFIRMED（R1 + R2 独立重发现）；evmone `state.cpp:632/:636` 顺序经主对话直读复核。

### D-04 GAS_LIMIT_REACHED 等块级错误降级为失败 receipt

- **位置**：`opstack/OpDepositTx.cpp:67-74`
- **严重度**：🔴 共识级
- **机理**：`err` 分支把 `validate_transaction` 的**所有**错误码一律转成失败 receipt（rollback + nonce++ + gasUsed=gasLimit），未区分块级错误。
- **对照**：op-geth 把 `ErrGasLimitReached` 从 deposit 的"失败 receipt 化"豁免中排除（`core/state_transition.go:486` 区域）——它向上冒泡为**块不可构建/无效**。
- **失败场景**：一个 deposit 的 gasLimit 超过区块剩余 gas：op-geth 判整块无效；本实现照常出一个失败 receipt 并继续 → **接受 op-geth 拒绝的区块**。
- **验证**：CONFIRMED（R1）。

### D-05 CREATE 前不递增 nonce → 部署地址差一 / nonce=0 断言崩溃

- **位置**：`opstack/OpDepositTx.cpp:79-81`（nonce 递增在 `:85/:92`，call 之后）
- **严重度**：🔴 共识级
- **机理**：evmone `Host::prepare_message` 的约定是调用方**先** nonce+1，CREATE 地址用 `nonce - 1` 计算（`test/state/host.cpp:239-240`：`// Nonce was already incremented, but creation calculation needs non-incremented value` + `assert(sender_acc.nonce != 0)`）。`runDeposit` 在 `host.call` 之后才写 `preNonce + 1`。普通路径 `OpTransition.cpp:171` 遵守了约定。
- **对照**：op-geth `evm.Create` 用当前 nonce N 计算合约地址。
- **失败场景**：合约创建型 deposit（`dep.to == nullopt`）：nonce=N 的账户 → 地址按 N-1 计算，与 op-geth 差一；**nonce=0 的新账户 → debug 构建 `assert` 直接崩溃，release 构建下溢到 2⁶⁴-1，部署到垃圾地址**。receipt、state diff、后续一切对该合约的交互全部错位。现有测试无一覆盖 `to=nullopt` 的 deposit。
- **验证**：CONFIRMED（R2）；`host.cpp:239-240` 原文经主对话直读复核。

### D-06 deposit receipt 类型标 legacy 而非 0x7E

- **位置**：`opstack/OpDepositTx.cpp:47`（tx.type）、`:65`（receipt.type）
- **严重度**：🔴 共识级
- **机理**：`receipt.type = Transaction::Type::legacy`。任何按 `receipt.type` 做 EIP-2718 typed envelope 编码的 receipts-root 计算（如 evmone `mpt_hash(receipts)`，ETH 路径 harness 的现行做法）都不会写 0x7E 前缀。`OpDepositReceipt` 虽然携带 `deposit_nonce/version`（`:99`），但 `receipt.type` 本身声称 legacy，无法弥补。
- **对照**：op-geth deposit receipt 是 0x7E typed receipt，Canyon+ 还含 `depositNonce`/`depositReceiptVersion` 字段。
- **失败场景**：**每一个含 deposit 的 OP 区块**（OP 链每块必有 L1 attributes deposit）receipts root 与规范块头不一致 → 块校验失败。
- **验证**：CONFIRMED（R2）。

### D-07 deposit receipt 从不计算 logs bloom

- **位置**：`opstack/OpDepositTx.cpp:64`（logs 赋值在 `:88/:95`，无 bloom 计算）
- **严重度**：🔴 共识级
- **机理**：baseline `transition()` 与本模块 `opTransition()` 在设置 logs 后都调 `compute_bloom_filter`；`runDeposit` 没有，receipt 顶着默认全零 bloom。
- **对照**：op-geth 为 deposit receipt 正常生成 bloom。
- **失败场景**：发出事件的成功 deposit（如 L1Block 更新、桥接 finalize 日志）：块头 logsBloom 与 receipts root 双错；RPC 日志/布隆过滤静默漏掉全部 deposit 事件。
- **验证**：CONFIRMED（R2）。

### D-08 deposit 不解析 EIP-7702 委托

- **位置**：`opstack/OpDepositTx.cpp:26`（`build_deposit_message` 硬编码 `code_address = recipient`）
- **严重度**：🔴 共识级
- **机理**：baseline `transition()` 顶层调用 `get_delegate_address` 解析委托并设 `code_address`/`EVMC_DELEGATED`（evmone `state.cpp:615-623`）；`runDeposit` 的消息构造丢了这一步。普通路径 `OpTransition.cpp:206` 后有对应处理。
- **对照**：op-geth 在 `evm.Call` 内解析 7702 委托，执行委托目标的代码。
- **失败场景**：Isthmus+（7702 生效）下 deposit 调用带委托标记的 EOA：这里把 `0xef0100‖addr` 标记字节当代码执行 → 撞 0xEF 非法指令立即失败（失败 receipt、gas 吃光）；op-geth 执行委托代码成功。用户的 L1→L2 调用意图丢失。
- **验证**：CONFIRMED（R2）。

### D-09 deposit 缺 EIP-2929/3651 预热序幕

- **位置**：`opstack/OpDepositTx.cpp:78`（`host.call` 前无任何预热）
- **严重度**：🔴 共识级
- **机理**：baseline `transition()` 在执行前置 sender WARM、`access_account(*tx.to)`、Shanghai+ 预热 coinbase；`OpTransition.cpp:194-203` 照做了；`runDeposit` 全部缺失。
- **对照**：op-geth 对 deposit 同样执行 `statedb.Prepare`（Berlin/Shanghai 规则）。
- **失败场景**：deposit 代码回访 sender/recipient/coinbase（`BALANCE(ORIGIN)`、CALL 回自身、给 coinbase 转账等）：每次多收 2600-100=2500 gas 冷访问费 → gasUsed 与 op-geth 分歧（receipts-root 分歧）；gas 卡得紧的 deposit 在 op-geth 成功、在这里 OOG。
- **验证**：CONFIRMED（R2）。

---

## 第二组：fork/host 配置（D-10 – D-12）

### D-10 `disable_prague_requests` 死配置

- **位置**：`opstack/OpForkSchedule.cpp:12,27,62,77,92`（赋值）；`include/bcos-evm-ref/opstack/OpForkSchedule.h:25`（声明）
- **严重度**：🟡 共识风险（未接线）
- **机理**：5 个 fork config 都设 `disable_prague_requests = true`，3 处单测断言它（`OpForkScheduleTest.cpp:11,23,48`），但**生产代码没有任何一处读取**（对照：同结构体的 `has_ecotone_l1_formula`、`has_da_footprint` 均被消费）。spec §4.3 第 10 点要求的 EIP-6110/7002/7251 requests 抑制没有实现，而单测使它看起来已实现。
- **对照**：op-geth 在 OP 链上不执行 Prague requests（6110/7002/7251）。
- **失败场景**：当前 deposit/tx 级路径不触发 requests，暂无实际分歧；一旦 OP 块级编排（§4.4）接入 ETH 侧 `runBlockFinalize` 而该开关仍无人读取，requests 会被错误执行 → 升级为共识级。
- **验证**：CONFIRMED（R1；R2 复确认读取点为零）。

### D-11 Granite/Holocene 丢 bn256Pairing 输入上限

- **位置**：`opstack/OpForkSchedule.cpp:38`（`graniteConfig()`/`holoceneConfig()` 复制 `fjordConfig()`，`precompiles = nullptr`）
- **严重度**：🔴 共识级
- **机理**：bn256Pairing（0x08）的 112687 字节输入上限由 **Granite 硬分叉引入**（op-geth Granite 起用 `bn256PairingGranite` 变体），`OpHost::call` 依赖 override 表执行该限制；Granite/Holocene 配置表为空 → 无上限。
- **对照**：op-geth Granite+ 对 >112687 字节的 0x08 调用立即失败并吃光 gas。
- **失败场景**：Granite/Holocene 链上一笔 >112687 字节的 0x08 调用（约 600 对 pairing、~20.4M gas，30M 块限内可执行）：这里完整执行并可成功；op-geth 判失败 → receipt 状态、gasUsed、post-state 三重分歧。
- **验证**：CONFIRMED（R2）。

### D-12 P256VERIFY（0x100）在 Isthmus/Jovian 不预热

- **位置**：`opstack/OpHost.cpp:93`（`call()` 派发 0x100，`access_account()` 未覆写）
- **严重度**：🔴 共识级
- **机理**：基类 `access_account` 只对 `evmone::is_precompile(rev, addr)` 为真的地址视为预热；evmone v0.21.0 里 0x100 门槛是 EVMC_OSAKA，而 Isthmus/Jovian 映射 rev=PRAGUE → 合约首次 CALL 0x100 收 2600 冷费。副作用：`access_account` 还会插入一个可擦除空账户，进入 state diff 的 `deleted_accounts`。
- **对照**：op-geth 经 `statedb.Prepare` 预热全部活跃 precompile（含 RIP-7212 的 0x100）→ 100 warm 费。
- **失败场景**：任何使用 P256VERIFY 的区块：gasUsed 差 2500 → receipt/cumulative-gas 分歧；外加 state diff 中的幽灵空账户。
- **验证**：CONFIRMED（R2）。

---

## 第三组：清理项（D-13 – D-14）

### D-13 OpFeeParams 每 tx 从存储重读 8 次

- **位置**：`opstack/OpValidate.cpp:41`
- **严重度**：🟢 清理（无语义影响）
- **机理**：`opValidateFromState` + `opTransitionFromState` 配对使用时各自从 L1Block 存储加载 OpFeeParams（各 4 次 storage 读，合计 8 次/tx），而 fee params 在整个区块内是常量。
- **验证**：CONFIRMED（R2）。

### D-14 消息构造逐字节重复 + FastLZ 双重压缩

- **位置**：`opstack/OpDepositTx.cpp:10-30` vs `opstack/OpTransition.cpp:137`（前者与后者逐字节相同，R1 verifier 逐段比对确认）；`opstack/OpReceiptMeta.cpp:32`（`deriveOpReceiptMeta` 对同一签名 envelope 重跑 FastLZ，`opValidate` 的 `computeL1Cost` 已压过一遍）
- **严重度**：🟢 清理（无语义影响；重复实现有漂移风险）
- **验证**：CONFIRMED（R1 重复项 + R2 FastLZ 项）。

---

## 附录 A：驳回与除名记录

记录目的：防止后续审查重复报告。

| 候选 | 处置 | 依据 |
|------|------|------|
| `StateDiffWriteback.h:4`——"公开签名用 `evmone::state::StateDiff` 却只 include `test_state.hpp`，依赖巧合的前向声明" | **驳回（误报）** | 该文件 `:4` 明确 `#include <test/state/state_diff.hpp>`，`StateDiff` 定义于 `state_diff.hpp:18`。前提为假（主对话直读驳回；R1 曾判 CONFIRMED，判错） |
| `spike/ReadAmplification.cpp:220`——`std::stoul(argv[1])` 无异常处理，非数字参数 `std::terminate` | **除名（不计入 14 条）** | 属实（R2 CONFIRMED），但为 spike 测量工具，非共识代码；顺手可修，不入共识缺陷清单 |

## 附录 B：覆盖声明

- 两轮合计 45 agent；第一轮 1 个 verifier 限流未完成，其负责的候选已由第二轮重扫覆盖。
- 审查为静态多 agent 代码审查 + 对抗验证，**不是机器差分**。deposit 路径至今没有任何 t8n 式差分覆盖（EEST 无 0x7E）——本台账不构成"仅此 14 条"的完备性声明。
