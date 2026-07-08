# OPStack Isthmus P1 质量项（OP-11–15）设计规格

**日期：** 2026-06-20  
**状态：** 已评审（用户要求 plan + execute）  
**前置：** P0 remediation（OP-01–09 + OP-09b）已完成

---

## 1. 目标

闭合 work-list P1 质量项 OP-11–15：加强测试 parity、清理测试假接线；**不引入新的 P0 行为变更**（除 OP-14 若写 metadata slot）。

**不包含：** OP-10（matrix 增补）——可并行另开。

---

## 2. 范围决策（用户隐含：对齐 op-geth / 审计建议）

| ID | 决策 | 交付 |
|----|------|------|
| OP-11 | **测试 only** | `OpStackFeeTest` 增补 op-geth min-bound + Solidity parity 向量 |
| OP-12 | **测试加强** | `L1AttributesDepositTest` literal `l1Fee` / `operatorFee` / recipient 余额 |
| OP-13 | **Phase 1** | `OpStackReceiptMeta` 增 scalar/constant；orchestration 填充；executor E2E 断言。**不**改 `TransactionReceipt` 协议/Tars（记 known gap） |
| OP-14 | **收窄** | `applySetterIsthmus` 写入 attributes 已解析的 metadata（number/timestamp/hash/sequence/batcherHash）到 op-geth Ecotone storage slot；扩展 `L1BlockPredeployTest`。不实现全量 `IL1Block` getter ABI（D5-2/D5-3 仍 🟡） |
| OP-15 | **测试清理** | 删除 8 个文件中冗余 `m_isIsthmus=true`；`RefundIsthmusTest` 保留（直连 `OpStackTxExecutor` 单元） |

---

## 3. 执行顺序

1. OP-15 — 最低风险，减少误导  
2. OP-11 — 纯测试  
3. OP-12 — 纯测试（依赖 fixture 已知值）  
4. OP-13 — 小结构体 + executor 路径  
5. OP-14 — 需 slot 常量对照 op-geth `rollup_cost.go`

---

## 4. Done 定义

每项：代码/测试变更 + 指定测试 PASS + work-list `[x]`。

---

## 5. 非目标

- `TransactionReceipt` 协议层 `operatorFeeScalar/Constant` RPC 字段（OP-13 Phase 2）
- 全量 L1Block getter ABI（`basefee()` selector 等）
- OP-10 capability-matrix（本 spec 外）

---

## 6. 成功标准

- OP-11–15 work-list 全部 `[x]`
- opstack 相关 ctest 通过
