# bcos-evm 源文件命名规范统一 — 设计文档

**日期:** 2026-06-24  
**范围:** `bcos-evm/` 源文件命名风格收敛（分四阶段 + Phase 1b）  
**词汇:** 取自 `codebase-design`（module / seam / leverage / locality）  
**关联:** ADR-005 · ADR-019 · `architecture-overview.md` · 新增 ADR-020

---

## 1. 问题陈述

`bcos-evm/` 约 240 个 `.h/.hpp/.cpp` 源文件存在 **三套命名风格并存**，与三层库架构（`eth/` 内核 + `bcos/` / `opstack/` 编排外壳）的物理边界不一致：

| 风格 | 占比 | 典型位置 | 架构语义冲突 |
| --- | --- | --- | --- |
| **PascalCase** | ~90% | `ExecuteViaEth.h`, `OrchestrationPipeline.h` | ✅ 文件名 = 模块/概念单元 |
| **camelCase** | 11 个 | `executeMessage.h`, `debitIntrinsicGas.h` | ❌ 暗示单函数 utility，与 ADR-019「深 module」矛盾 |
| **snake_case** | 6+ 个 | `BloomFilter.hpp`, `executor.hpp` | ❌ 遗留风格，与同级 PascalCase 混存 |

**附加问题：**

1. **扩展名混用：** 同一目录 `eth/state/` 内 `.h` 与 `.hpp` 并存（102 vs 16 文件）。
2. **重复 basename：** `Eip4844.h` 同时存在于 `eth/gas/`（blob gas 常量）与 `opstack/`（blob tx intent 校验），语义不同、名称相同。
3. **测试目录不对齐：** 5 个测试文件留在 `test/` 根目录，未镜像三层域边界。
4. **无 CI 门禁：** 新文件可继续引入 camelCase/snake_case，风格漂移无结构阻止。

**时机：** orchestration pipeline（ADR-019）刚落地，camelCase 头文件引用面尚可控；现在统一成本最低。

---

## 2. 目标与非目标

### 目标

1. 确立 **单一命名规范**（PascalCase + `.h`/`.cpp`），写入 ADR-020。
2. **Phase 1 + 1b** 消除活跃架构层 camelCase 违规，以及 `eth/state/` snake_case basename 违规（同 PR）。
3. **Phase 2** 测试目录镜像三层结构，根目录散落测试迁入域子目录。
4. 新增 CI 脚本 `check-filename-convention.sh`，挂入 `capability-gate.yml`。
5. 解决 `opstack/OpStackBlobTxIntent.h` 与 `eth/gas/Eip4844.h` 的 basename 冲突。

### 非目标

- **Phase 3 不在本计划实施：** `eth/state/` 已合规 basename 的 `.hpp → .h` 扩展名迁移（~153 引用，含 `transaction-executor/`）。
- **Phase 4 不在本计划实施：** `include/bcos-evm/*.hpp` 公共 API — 与上层 FISCO-BCOS 对齐后单独 ADR/PR。
- 不强制重命名所有测试文件的 `Eip*` vs `Eth*` vs `Bcos*` 前缀（仅规范新文件 + 文档约定）。
- 不改函数/类/命名空间标识符（仅文件名与 `#include` 路径）。
- 不改运行时语义。

### 已确认决策

| # | 议题 | 结论 |
| --- | --- | --- |
| Q1 | 命名策略 | **PascalCase + `.h`/`.cpp`**（方案 A） |
| Q2 | 实施范围 | **分四阶段**；Phase 1+1b+2 本计划，Phase 3/4 延后 |
| Q6 | eth/state/ 策略 | **Phase 1b** 清 snake_case basename；**.hpp 扩展名**留 Phase 3 |
| Q3 | orchestration camelCase | **改为 PascalCase**；文件名表模块组件，不表函数名 |
| Q4 | `opstack/OpStackBlobTxIntent.h` | **语义重命名** 为 `OpStackBlobTxIntent.h`（非仅大小写） |
| Q5 | 函数名是否跟随 | **否**；`debitIntrinsicGas()` 等函数名保持 camelCase（C++ 惯例），仅文件名 PascalCase |

---

## 3. 命名规范（ADR-020 摘要）

### 3.1 通用规则

| 规则 | 说明 |
| --- | --- |
| **大小写** | 源文件 basename 使用 **PascalCase**（首字母大写，内部词首大写） |
| **扩展名** | 实现文件 `.cpp`；头文件 **`.h`**（`.hpp` 仅限 Phase 3 遗留豁免区） |
| **语义** | 文件名反映 **模块/概念/类型**，不使用 camelCase 函数名作为文件名 |
| **目录** | 目录即域边界：`eth/`、`bcos/`、`opstack/`；`test/` 镜像同结构 |
| **测试** | `<Feature>Test.cpp`，推荐域前缀与目录一致（`test/bcos/Bcos*`、`test/opstack/OpStack*`） |

### 3.2 EIP 与前缀规则

| 类别 | 命名 | 示例 |
| --- | --- | --- |
| EIP 专项模块 | `Eip<N>.h` | `Eip7702.h`, `Eip4844.h` |
| 链域模块 | 层前缀 + 域名 | `FiscoPolicy.h`, `OpStackFee.h` |
| 编排管线组件 | `Orchestration*` 或动词 PascalCase | `DebitIntrinsicGas.h` |
| 域模型（跨 EIP） | 无前缀 | `AccessList.h`, `RevisionConfig.h` |

### 3.3 `eth/state/` Legacy Enclave 双轨策略

`eth/state/` 内文件分两类：

| 类别 | 文件 | Phase 1b | Phase 3 |
| --- | --- | --- | --- |
| **A — basename 已合规** | `State.hpp`, `EthHost.hpp`, `Transaction.hpp` 等 9 个 | 不动 | `.hpp → .h` |
| **B — snake_case 违规** | `bloom_filter.*`, `HashUtils.hpp`, `Errors.hpp`, `transition.*` | **PascalCase rename** | 随 A 类一并 `.hpp → .h` |

评审者打开 `ExecuteMessage.cpp` 时，include 列表应全为 PascalCase basename（扩展名暂可为 `.hpp`）。

### 3.4 CI 扩展名豁免（Phase 3/4 allowlist）

CI **强制** PascalCase basename；**仅豁免**以下路径的 `.hpp` 扩展名（不允许 snake_case basename）：

```
eth/state/*.hpp          # Phase 3：basename 已合规，扩展名待迁移
include/bcos-evm/*.hpp   # Phase 4：对外安装面
```

---

## 4. Phase 1 — 活跃架构层 rename 清单

**范围：** `eth/`（除 `eth/state/` 遗留）、`bcos/`、`opstack/`  
**方法：** `git mv` 保留历史；机械更新 `#include`、`CMakeLists.txt`、ADR-019 file layout 段。

### 4.1 大小写 rename（camelCase → PascalCase）

| 现路径 | 新路径 | 引用面（约） |
| --- | --- | --- |
| `eth/executeMessage.h` | `eth/ExecuteMessage.h` | **33** 文件（全库最多） |
| `eth/executeMessage.cpp` | `eth/ExecuteMessage.cpp` | CMake + 多处 test 直编 |
| `eth/execution/warmTransactionEntry.h` | `eth/execution/WarmTransactionEntry.h` | ~5 |
| `eth/orchestration/adoptEvmcResult.h` | `eth/orchestration/AdoptEvmcResult.h` | ~3 |
| `eth/orchestration/debitIntrinsicGas.h` | `eth/orchestration/DebitIntrinsicGas.h` | **5** 文件 |
| `eth/orchestration/buildExecuteMessageInput.h` | `eth/orchestration/BuildExecuteMessageInput.h` | ~3 |
| `eth/orchestration/captureSettlementSnapshot.h` | `eth/orchestration/CaptureSettlementSnapshot.h` | ~3 |
| `eth/orchestration/normalizeIncludedTxVmerr.h` | `eth/orchestration/NormalizeIncludedTxVmerr.h` | ~2 |

**合计：** 9 个文件 rename，~50 处 `#include` 更新（含 `test/`、`specs-tests/`、`include/bcos-evm/`；`executeMessage.h` 实测 33 文件、`debitIntrinsicGas.h` 5 文件）。

### 4.2 语义 rename（basename 冲突）

| 现路径 | 新路径 | 理由 |
| --- | --- | --- |
| `opstack/OpStackBlobTxIntent.h` | `opstack/OpStackBlobTxIntent.h` | 内容为 `hasBlobTxIntent`/`isValidVersionedHash`，与 `eth/gas/Eip4844.h`（blob gas 数学）语义不同 |

引用：`opstack/OpStackPreCheck.cpp`（1 处）。

### 4.3 CMake 更新

- `bcos-evm/CMakeLists.txt` L13：`executeMessage.cpp` → `ExecuteMessage.cpp`
- `bcos-evm/test/CMakeLists.txt`：所有 `../eth/executeMessage.cpp` 直编路径（~8 处）

### 4.5 Phase 1b — eth/state snake_case 清理（同 PR）

**范围：** `eth/state/` 内 B 类文件（6 个）  
**方法：** `git mv`；更新 `eth/state/` 内部互引、`ExecuteMessage.cpp`、`test/CMakeLists.txt` 直编路径。

| 现路径 | 新路径 |
| --- | --- |
| `eth/state/BloomFilter.hpp` | `eth/state/BloomFilter.hpp` |
| `eth/state/BloomFilter.cpp` | `eth/state/BloomFilter.cpp` |
| `eth/state/HashUtils.hpp` | `eth/state/HashUtils.hpp` |
| `eth/state/Errors.hpp` | `eth/state/Errors.hpp` |
| `eth/state/Transition.hpp` | `eth/state/Transition.hpp` |
| `eth/state/Transition.cpp` | `eth/state/Transition.cpp` |

引用面：基本限于 `eth/state/` 内部 + `test/CMakeLists.txt` 直编（~8 处 `BloomFilter.cpp` / `Transition.cpp`）。

### 4.6 文档更新

- `bcos-evm/docs/adr/019-orchestration-pipeline.md` §File layout
- `bcos-evm/docs/architecture-overview.md`（Legacy Enclave 注记）
- `bcos-evm/docs/adr/020-filename-convention.md`

### 4.7 验收

- [ ] 全量 `bcos-evm` 测试编译通过（零语义变更）
- [ ] `grep -r 'executeMessage\.h\|debitIntrinsicGas\.h\|bloom_filter'` 零命中
- [ ] `check-filename-convention.sh` Phase 1+1b 范围 PASS

---

## 5. Phase 2 — 测试目录镜像

**范围：** `bcos-evm/test/`  
**原则：** 物理目录 = 逻辑域；`test/` 根目录不再新增 `.cpp` 测试。

### 5.1 文件迁移

| 现路径 | 新路径 | 域 |
| --- | --- | --- |
| `test/EthHostExtensionHooksTest.cpp` | `test/eth/EthHostExtensionHooksTest.cpp` | eth 内核 |
| `test/ExecuteMessageSmokeTest.cpp` | `test/eth/ExecuteMessageSmokeTest.cpp` | eth 内核 |
| `test/ExecuteViaHostSmokeTest.cpp` | `test/bcos/ExecuteViaHostSmokeTest.cpp` | bcos 编排 |
| `test/FiscoHostExtensionTest.cpp` | `test/bcos/FiscoHostExtensionTest.cpp` | bcos 编排 |
| `test/StateJournalRevertTest.cpp` | `test/state/StateJournalRevertTest.cpp` | state 层 |

### 5.2 CMake 更新

`test/CMakeLists.txt` 中 5 处 `add_executable` 源路径改为子目录路径。CTest `NAME` 保持不变（避免 CI 历史断裂）。

### 5.3 测试命名约定（文档化，非强制 bulk rename）

| 目录 | 推荐前缀 | 示例 |
| --- | --- | --- |
| `test/eth/` | `Eip*` / `Eth*` / 无前缀 | `Eip7623PrecheckTest`, `OrchestrationPipelineTest` |
| `test/bcos/` | `Bcos*` | `Bcos7702ExecuteViaHostPropagationTest` |
| `test/opstack/` | `OpStack*` / `Eip*`（OP 特有 EIP 行为） | `OpStackSettlementTest` |
| `test/state/` | 无前缀或 `Eip*` | `PragueStateTest`, `Eip2929AccessHostTest` |

已有 `EthEip1559GasTest` 等混合前缀 **不强制改**；新测试应遵循上表。

### 5.4 验收

- [ ] `test/` 根目录零 `.cpp` 文件
- [ ] 全量 CTest PASS
- [ ] CI filename gate 覆盖 `test/`

---

## 6. Phase 3 — eth/state `.hpp → .h`（延后）

**触发条件：** 单独 ADR + 与 `transaction-executor/` 协调 PR。

| 区域 | 现状 | 目标 |
| --- | --- | --- |
| `eth/state/` | `State.hpp`, `BloomFilter.hpp` 等（basename 已合规） | 统一扩展名 `.h` |

**风险：** ~153 处 `#include` 跨越 `bcos-evm` 全库；`transaction-executor/` ~10 处。

---

## 7. Phase 4 — 公共 API（延后）

| 区域 | 现状 | 目标 |
| --- | --- | --- |
| `include/bcos-evm/` | `executor.hpp`, `fisco_executor.hpp` | 与 repo 上层命名对齐后迁移 |

---

## 8. CI 门禁设计

### 8.1 脚本：`bcos-evm/tools/ci/check-filename-convention.sh`

```bash
# 伪代码逻辑
SCOPED_DIRS="eth bcos opstack test specs-tests"
HPP_ALLOWLIST_PREFIXES="eth/state/ include/bcos-evm/"

for each *.h *.hpp *.cpp in SCOPED_DIRS (recursive):
  # basename 检查：全库强制 PascalCase，不允许 snake_case / camelCase
  basename must match ^[A-Z][A-Za-z0-9]*\.(h|hpp|cpp)$
  if basename contains '_': FAIL

  # 扩展名检查：仅 allowlist 路径允许 .hpp
  if extension is .hpp and path not in HPP_ALLOWLIST_PREFIXES: FAIL
  if extension is .h and path in scoped dirs (non-allowlist): OK
```

### 8.2 挂接

在 `.github/workflows/capability-gate.yml` 的 `matrix-lint` job 新增 step：

```yaml
- name: Filename convention gate
  run: bash bcos-evm/tools/ci/check-filename-convention.sh
```

触发路径与现有 `bcos-evm/eth/**` 等一致。

---

## 9. 迁移策略

### 9.1 执行顺序

1. 新增 ADR-020 + CI 脚本（与 rename 同 PR）
2. Phase 1 + 1b rename batch（单 PR，纯机械变更）
3. Phase 2 test 迁移（可同 PR 或紧随 PR）
4. 文档同步

### 9.2 工具

- `git mv` 保留 blame 历史
- 全局 `#include` 替换（rg + sed，人工 review diff）
- **不**使用符号链接兼容层（YAGNI，增加 indirection）

### 9.3 回滚

纯 rename PR，回滚 = revert 单 commit；无 schema/ABI 变更。

---

## 10. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 遗漏 `#include` 导致编译失败 | CI 全量编译；rename PR 禁止逻辑变更 |
| 外部分支基于旧路径开发 | CHANGELOG/PR 描述列出 rename 表；git 可检测 rename |
| ADR-019 文档与代码不同步 | 同 PR 更新 ADR-019 file layout |
| Phase 3 长期搁置导致双轨 | CI 豁免区有明确 TODO + ADR-020 登记 |

---

## 11. 开放问题

无。Q1–Q6 已在 brainstorming / grilling 中确认。

---

## 附录 A：Phase 1 + 1b 完整文件对照

```
eth/executeMessage.h                          → eth/ExecuteMessage.h
eth/executeMessage.cpp                        → eth/ExecuteMessage.cpp
eth/execution/warmTransactionEntry.h          → eth/execution/WarmTransactionEntry.h
eth/orchestration/adoptEvmcResult.h          → eth/orchestration/AdoptEvmcResult.h
eth/orchestration/debitIntrinsicGas.h        → eth/orchestration/DebitIntrinsicGas.h
eth/orchestration/buildExecuteMessageInput.h → eth/orchestration/BuildExecuteMessageInput.h
eth/orchestration/captureSettlementSnapshot.h→ eth/orchestration/CaptureSettlementSnapshot.h
eth/orchestration/normalizeIncludedTxVmerr.h → eth/orchestration/NormalizeIncludedTxVmerr.h
opstack/OpStackBlobTxIntent.h                            → opstack/OpStackBlobTxIntent.h
eth/state/BloomFilter.hpp                   → eth/state/BloomFilter.hpp
eth/state/BloomFilter.cpp                   → eth/state/BloomFilter.cpp
eth/state/HashUtils.hpp                     → eth/state/HashUtils.hpp
eth/state/Errors.hpp                         → eth/state/Errors.hpp
eth/state/Transition.hpp                     → eth/state/Transition.hpp
eth/state/Transition.cpp                     → eth/state/Transition.cpp
```

## 附录 B：不变项

以下 **保持不动**（Phase 1b 之后、Phase 3 之前）：

- 函数名：`executeMessage()`, `debitIntrinsicGas()`, `runOrchestration()` 等
- 类/struct 名：`OrchestrationContext`, `ExecuteMessageInput` 等
- `eth/state/` A 类文件扩展名（`State.hpp` 等，Phase 3 再改）
- `include/bcos-evm/` 全部文件（Phase 4）
- 已有测试 basename（除非随 `#include` 路径必须改）
