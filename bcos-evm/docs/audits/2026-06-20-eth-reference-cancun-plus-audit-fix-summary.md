# ETH Reference CANCUN+ 审计阻断项修复摘要

**日期：** 2026-06-20  
**分支：** `feat-evm-refactor`  
**审计报告：** `2026-06-20-eth-reference-cancun-plus-audit.md`  
**修复 spec：** `docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-fix-design.md`

---

## 结论

Part 1 全部 **7 项 🔴 阻断** 已通过 `bcos-evm/eth/**` 内核修复关闭。BCOS `executeViaHost` 经共享 `executeMessage()` 自动继承 kernel 变更，无 `bcos/` 内核重复实现。

**复审计结果（2026-06-20）：** **⚠️ 有条件通过** — 0×🔴，2×🟡 非阻断。详见 [`2026-06-20-eth-reference-cancun-plus-audit-reaudit.md`](2026-06-20-eth-reference-cancun-plus-audit-reaudit.md)。

> **P1：** 7702 E2E、7623、1153 已 PASS。  
> **P2：** `Eip2929OpcodeGasTest`（5 cases，BALANCE/SLOAD cold/warm literal）已 PASS；2929 升为 **✅ impl + ✅ test**。

---

## P0 修复一览

| # | 能力 | 修复 | ETH 测试 | BCOS 继承测试 |
|---|------|------|----------|---------------|
| P0-1 | EIP-7702 revision enable | `EthPolicy` PRAGUE+ 设 `eip7702=true` | `Eip7702ApplyAuthorizationEthTest` | `Bcos7702ExecuteViaHostPropagationTest` |
| P0-2 | EIP-6780 SELFDESTRUCT | `EthHost::selfdestruct` + same-tx CREATE 跟踪 | `ExecuteViaEthFixtureTest` | `Bcos6780SelfdestructTest` |
| P0-3 | EIP-2537 MSM gas | `BlsGas.h` 128 项折扣表 wired 到 TE dispatch | `Eip2537KernelTest` (k=2 → 22776) | `Bcos2537MsmGasTest` |
| P0-4 | EIP-7212 (0x0100) | `executeP256Verify` in `EthPrecompiles` | `Eip7212KernelTest` | `Bcos7212ExecuteViaHostTest` |
| P0-5 | EIP-7823 modexp bounds | `shouldRejectModexpEip7823` in TE dispatch | `Eip7823ModexpRejectTest` | `Bcos7823ModexpRejectTest` |
| P0-6 | Prague 预编译门控 | 统一 `PrecompileActive.h` | `EipPrecompileRevisionGateTest` | `BcosPrecompileRevisionGateTest` |

---

## capability-matrix.md 同步（§9）

| 行 | 变更 |
|----|------|
| EIP-7702 authorization apply | ETH 列：profile 门控已满足，baseline-reachable |
| EIP-6780 SELFDESTRUCT (kernel) | 新增行：`inherited` |
| EIP-2537 | 移除 TE MSM 待接；脚注含 `BlsGas.h` |
| EIP-7212 | `unsupported` → `inherited`（ETH） |
| RevisionConfig `eip7823` | profile-only → TE consumer wired |
| builtin 0x01–0x11 | 脚注：`PrecompileActive.h` revision 门控 |

---

## 回归测试

```bash
cd build
ctest -R "Eip7702ApplyAuthorizationEth|EipPrecompileRevisionGate|BcosPrecompileRevisionGate|Eip2537|Bcos2537|Eip7823|Bcos7823|Eip7212|Bcos7212|Bcos6780|RevisionConfigProfile|ExecuteViaEthFixture|Eip2929|Eip7623|Eip1153|Bcos7702" --output-on-failure
```

基线：`RevisionConfigProfileTest`、`Eip2929AccessHostTest`、`Eip2929OpcodeGasTest`、`ExecuteViaEthFixtureTest` 全绿。

---

## P1 补测（2026-06-20，未提交）

| 项 | 内核 | ETH 测试 |
|----|------|----------|
| 7702 delegation E2E | `ExecuteMessage.cpp` / `EthHost.cpp` `resolveExecutableCode` | `stEIP7702_delegation.json` + `ExecuteViaEthFixtureTest` |
| 7623 ETH precheck | `ExecuteViaEth.cpp`（已有） | `Eip7623PrecheckTest` |
| 1153 transient purge | `State.cpp` `build_diff()` 剥离 `transientStorage` | `Eip1153TransientStorageTest`（3 cases） |

## P2 补测（2026-06-20，未提交）

| 项 | 测试 | 断言 |
|----|------|------|
| EIP-2929 opcode gas | `Eip2929OpcodeGasTest`（5 cases） | `BALANCE` cold 2600 / warm 100；`SLOAD` cold 2100 / warm 100；`warm_access=false` 双 cold 5206 |

```bash
cd build
./bcos-evm/test/Eip7623PrecheckTest
./bcos-evm/test/Eip1153TransientStorageTest
./bcos-evm/test/Eip2929OpcodeGasTest
./bcos-evm/test/ExecuteViaEthFixtureTest
```
