# OPStack Isthmus 审计清单

**来源：** `capability-matrix.md` OPStack 列 + grill-me 增补清单  
**ETH 交叉引用：** `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md` **已存在** — inherited 行可交叉引用（非默认 blocked）

## 增补能力（待 matrix 合入）

| # | Capability | Layer | Depth | Task |
|---|------------|-------|-------|------|
| S1 | OPStack operator fee (Isthmus) | orchestration | 深审 | Task 3 |
| S2 | L1 attributes system deposit | orchestration | 深审 | Task 5 |
| S3 | Isthmus executor integration wiring | executor-integration | 深审 | Task 1 |

## Matrix OPStack 列

| # | Capability | Layer | OPStack status | Depth | Task |
|---|------------|-------|----------------|-------|------|
| 1 | EIP-2929 runtime warm | kernel | inherited | smoke | Task 8 |
| 2 | EIP-2929 tx-entry destination warm | tx input | inherited | smoke | Task 8 |
| 3 | EIP-2929 tx-entry coinbase warm | tx input | inherited | smoke | Task 8 |
| 4 | EIP-7702 authorization apply | kernel | inherited | smoke | Task 8 |
| 5 | EIP-7702 tx field propagation | tx input | inherited | smoke | Task 8 |
| 6 | EIP-7702 revision enable | revision profile | inherited | smoke | Task 1/8 |
| 7 | EIP-7702 precheck + intrinsic gas | orchestration | explicit | 深审 | Task 7 |
| 8 | EIP-7623 entry precheck | orchestration | explicit | 深审 | Task 6 |
| 9 | EIP-7623 settlement / floor gas | orchestration | deviation | 深审 | Task 6 |
| 10 | EIP-2537 precompiles | kernel | inherited | smoke | Task 8 |
| 11 | EIP-7212 precompile | kernel | unsupported | ⚪ | Task 9 |
| 12 | EIP-4844 revision profile | revision profile | inherited | smoke | Task 1/8 |
| 13 | EIP-4844 blob orchestration | orchestration | explicit | 深审 | Task 7 |
| 14 | builtin precompiles 0x01–0x11 | kernel | inherited | smoke | Task 8 |
| 15 | chain precompile routing (L1Block) | host extension | deviation | 深审 | Task 5 |
| 16 | OPStack deposit tx | orchestration | explicit | 深审 | Task 4 |
| 17 | RevisionConfig warm_access | revision profile | feature-gated | profile-only | Task 9 |
| 18 | RevisionConfig eip1153 | revision profile | inherited | smoke/profile | Task 8/9 |
| 19 | RevisionConfig eip5656 | revision profile | inherited | evmone-delegated | Task 8/9 |
| 20 | RevisionConfig eip6780 | revision profile | inherited | smoke | Task 8 |
| 21 | EIP-6780 SELFDESTRUCT | kernel | inherited | smoke | Task 8 |
| 22 | RevisionConfig eip1559 | revision profile | feature-gated | profile-only | Task 9 |
| 23 | RevisionConfig eip3651 | revision profile | feature-gated | profile-only | Task 9 |
| 24 | RevisionConfig prague_post_execution | revision profile | unsupported | ⚪ | Task 9 |
| 25 | RevisionConfig eip7823 | revision profile | feature-gated | ⚪ Isthmus | Task 9 |
| 26 | OPStack receipt metadata | orchestration | explicit | 深审 | Task 6 |

## 明确 ⚪（不审计实现，仅确认未激活）

| Capability | OPStack status | Task |
|------------|----------------|------|
| BCOS fixed 21000 gas debit | unsupported | Task 9 |
| BCOS auth check | unsupported | Task 9 |
| BCOS value transfer | unsupported | Task 9 |
| BCOS CREATE nonce persist | unsupported | Task 9 |

**合计：** 3 增补 + 26 matrix 行 + 4 BCOS ⚪ = 33 可追踪项
