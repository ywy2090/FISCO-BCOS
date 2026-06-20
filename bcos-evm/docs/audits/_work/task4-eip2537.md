# Task 4 — Prague 簇审计笔记（EIP-2537 BLS12-381）

**日期：** 2026-06-20  
**范围：** inventory #10；ETH reference `executeMessage` / `EthPrecompiles` 路径  
**参考：** geth v1.17.3 `params/protocol_params.go` + `core/vm/contracts.go`

---

## Step 1: 导出 geth 128 项折扣表

**源：** `go-ethereum/params/protocol_params.go`

| 符号 | 长度 |
|------|------|
| `Bls12381G1MultiExpDiscountTable` | 128 |
| `Bls12381G2MultiExpDiscountTable` | 128 |

**输出：** `_work/eip2537-geth-discounts.txt`（256 行：`G1MSM[0..127]` + `G2MSM[0..127]`）

---

## Step 2: 导出 FB 折扣表

**源（任务指定）：** `bcos-evm/eth/precompiled/EthBuiltinRegistry.cpp` — `bls12_g1msm` / `bls12_g2msm` pricer 内 `DISCOUNTS[]`

**输出：** `_work/eip2537-fb-discounts.txt`

**备注：** FISCO `PrecompiledImpl` / `PrecompiledManager` 经 `builtinPricerBySuffix` 使用该表；**不在** ETH reference TE 路径上。

---

## Step 3: 逐项 diff（128×2，无抽样）

```bash
rtk diff _work/eip2537-geth-discounts.txt _work/eip2537-fb-discounts.txt
# → Files are identical
```

| 表 | 条目 | 不匹配 |
|----|------|--------|
| G1MSM | 128 | 0 |
| G2MSM | 128 | 0 |
| **合计** | **256** | **0** |

**EthBuiltinRegistry 静态表 vs geth：** ✅ 完全一致。

---

## Step 4: 预编译地址 0x0b–0x11 与测试

### 地址映射

| Index | 地址 suffix | 预编译 | FB 注册 |
|-------|-------------|--------|---------|
| 0x0b | `...000b` | G1Add | `EthBuiltinRegistry.cpp:577-578`；`PrecompiledAddress.h:48-49` |
| 0x0c | `...000c` | G1MSM | 同上 |
| 0x0d | `...000d` | G2Add | 同上 |
| 0x0e | `...000e` | G2MSM | 同上 |
| 0x0f | `...000f` | Pairing | 同上 |
| 0x10 | `...0010` | MapFp→G1 | 同上 |
| 0x11 | `...0011` | MapFp2→G2 | 同上 |

geth `PrecompiledContractsPrague`（`contracts.go:137-143`）同址。

### 固定 gas 常量（非 MSM）

| 预编译 | FB `EthBuiltinRegistry` | geth `protocol_params.go` | FB `EthPrecompiles.cpp` |
|--------|-------------------------|----------------------------|-------------------------|
| G1Add 0x0b | 375 | 375 | 375 ✅ |
| G2Add 0x0d | 600 | 600 | 600 ✅ |
| Pairing 0x0f | 37700 + 32600×k | 同 | 同 ✅ |
| MapG1 0x10 | 5500 | 5500 | 5500 ✅ |
| MapG2 0x11 | 23800 | 23800 | 23800 ✅ |

### MSM gas 公式（TE 路径 🔴）

**geth / EthBuiltinRegistry（一致）：**

```
gas = k * Bls12381G{1,2}MulGas * discountTable[min(k,128)-1] / 1000
```

**EthPrecompiles.cpp `precompileGasCost`（`executeMessage` / `EthHost::routeCall` 实际路径）：**

```cpp
case 0x000c: return 12000 * (input.size() / 160);   // 无折扣
case 0x000e: return 22500 * (input.size() / 288);   // 无折扣
```

示例 k=2 G1MSM：geth = `2×12000×949/1000 = 22776`；EthPrecompiles = `24000`（多收 1224 gas）。

### 测试运行

```bash
cd build && ./bcos-evm/test/Eip2537KernelTest --log_level=test_suite
# PASS — stBLS_add_precompile_0x0b_via_executeMessage
```

- **Fixture：** `stBLS_add.json` → call `0x0b` G1Add，256-byte input；`EVMC_PRAGUE` + `eip2537=true`
- **断言：** status `EVMC_SUCCESS` + output bytes（不测 gas；`expected.gas_used=0` 占位）
- **缺口：** 无 G1MSM/G2MSM gas 回归；无 0x0c–0x11 全地址 fixture

---

## Step 5: 判定汇总

| 检查项 | 结果 |
|--------|------|
| EthBuiltinRegistry 128×2 折扣表 vs geth | ✅ 256/256 匹配 |
| TE 路径 MSM 折扣表 wired | 🔴 EthPrecompiles 线性 gas |
| 0x0b–0x11 地址注册 | ✅ |
| G1Add 执行 + fixture | ✅ PASS |
| Prague revision 门控（Task 2 已知） | 🟡 CANCUN revision 仍可 dispatch 0x0b–0x11 |

**Task 状态：** **DONE_WITH_CONCERNS**

- 静态表（EthBuiltinRegistry）合规 ✅
- ETH reference 运行时 MSM gas 🔴 偏离 geth
- revision 门控 🟡（Task 2 已记录）

**建议：**

1. `EthPrecompiles::precompileGasCost` 0x000c/0x000e 复用与 geth 相同的 128 项表（可共享常量或委托 `builtinPricerBySuffix`）。
2. 增加 G1MSM/G2MSM gas 单元测试（k=1,2,128 边界）。
3. `isBuiltinPrecompileAddress` / dispatch 增加 `revision >= EVMC_PRAGUE` 门控。
