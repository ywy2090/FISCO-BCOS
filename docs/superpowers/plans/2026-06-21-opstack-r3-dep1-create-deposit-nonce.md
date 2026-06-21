# R3-DEP-1 CREATE Deposit Nonce Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 闭合 Wave 3 审计项 R3-DEP-1——CREATE deposit 成功后 sender nonce 恰好 +1。

**Architecture:** Test-first（方案 A）。在 `opStackExecuteViaHost` 层构造 `EVMC_CREATE` + `depositTx.to=nullopt`，断言 nonce 与 `depositNonce`；失败再最小修复编排层 bump。

**Tech Stack:** Boost.Test, evmone, `InMemoryStateView`, `opStackExecuteViaHost`

**Spec:** `docs/superpowers/specs/2026-06-21-opstack-r3-dep1-create-deposit-nonce-design.md`

---

### Task 1: 新增 DepositCreateNonceTest

**Files:**
- Create: `bcos-evm/test/opstack/DepositCreateNonceTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Step 1:** 复制 `DepositMintTest` fixture 模式，改 `message.kind=EVMC_CREATE`，`recipient/code_address` 为零地址，`depositTx.to=std::nullopt`，init code `60006000f3`。

**Step 2:** 断言 `EVMC_SUCCESS`、sender nonce 3→4、`depositNonce==3`。

**Step 3:** CMake 注册 `DepositCreateNonce` 可执行与 CTest。

**Step 4:** 运行 `ctest -R DepositCreateNonce -V`。

**Step 5:** 若失败按 spec §5 修 `OpStackExecuteViaHost.cpp`；若 PASS 零 prod 改动。

---

### Task 2: 回归与文档

**Files:**
- Modify: `bcos-evm/docs/audits/2026-06-21-opstack-isthmus-reaudit-wave3.md`
- Modify: `bcos-evm/docs/audits/_work/task4-deposit.md`
- Modify: `bcos-evm/capability-matrix.md`（可选一行）

**Step 1:** 跑 `DepositMint`、`DepositNoFeeRouting`、`DepositTxPreCheck`。

**Step 2:** R3-DEP-1 → CLOSED，引用测试文件。
