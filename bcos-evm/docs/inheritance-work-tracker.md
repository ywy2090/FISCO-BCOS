# Inheritance work tracker

**Branch:** `feat-evm-refactor`  
**Normative matrix:** `bcos-evm/capability-matrix.md`  
**Design:** `docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md`

Legend: `[x]` done · `[~]` partial · `[ ]` open

---

## P0 — Phase 1 收尾

- [x] **1** 补全 capability matrix（RevisionConfig 字段、编排域行）
- [x] **2** 矩阵缺口行（auth、value transfer、nonce、receipt 等）
- [x] **3** ADR-004 RevisionConfig 消费 vs profile-only
- [x] **4** ADR-005 编排域边界
- [x] **5** PR 审查 checklist（`bcos-evm/docs/inheritance-pr-review-checklist.md`）
- [x] **6** `capability-gate` workflow + `check-capability-matrix.sh`（矩阵 lint + RevisionConfig→ProfileTest gate + **full opstack CTest** per ADR-010；workflow 已入分支，本地 PASS，**远程 CI 待首跑**）

## P1 — Phase 2 前置

- [x] **11** BCOS EIP-7702 决策（ADR-006：feature-gated + Web3 0x04）
- [x] **16** Tx input propagation — builder 单测 + **`Bcos7702ExecuteViaHostPropagationTest`** + OP **`OpStack7702ExecuteViaHostPropagationTest`**
- [x] **17** `RevisionConfigProfileTest` — Eth/Fisco 全 fork 表 + Isthmus sparse profile

## P2 — Phase 2/3

- [x] **12** BCOS EIP-7702 字段接线
- [x] **13** BCOS 7702 编排延后（ADR-006）
- [x] **14** OPStack 显式 `txProps`
- [x] **15** 三 orchestrator 7702 字段命名对齐
- [x] **19–21** Profile 审计 / `RevisionConfigProfileTest` 全表

## P3 — 质量与长期

### 测试债务 28–35

- [~] **28** BCOS baseline — smoke + 7702 + imported fixture pipeline
- [x] **29** BCOS 7702 — **`Bcos7702ExecuteViaHostPropagationTest`**
- [x] **30** BCOS 7623 precheck — **`Bcos7623PrecheckTest`**
- [x] **31** BCOS 21000 gas deviation
- [x] **32** OP L1Block HostExtension E2E — **`L1BlockGetterTest`**
- [x] **33** EIP-7212 unsupported
- [x] **34** `stEIP7702_delegation.json` fixture pipeline — **`ExecuteViaHostImportedFixtureTest`**（非 7702 delegation E2E）
- [~] **35** 矩阵 Test ref — 主要 explicit/deviation 行已补；value transfer / CREATE nonce 仍为 `—`

### Phase 4 helpers 25–27

- [~] **25–27** `TxFeaturePrepare` 已落地；其余 helper 待提取

### 架构 gap 36–38

- [x] **36–38** 已文档化于 `architecture-known-gaps.md`

### ADR-007

- [x] **Web3 decoder** TE → `bcos-executor` 依赖决策（ADR-007；迁移 deferred）

---

**Last updated:** 2026-06-21 (FIX-15 Wave 2 sign-off — capability-gate workflow in branch; remote CI first run pending)
