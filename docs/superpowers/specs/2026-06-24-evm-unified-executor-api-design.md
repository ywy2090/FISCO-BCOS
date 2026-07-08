# [Superseded] bcos-evm 统一执行 API（泛型 Profile）

**Status:** Superseded  
**Date:** 2026-06-24  
**Superseded by:** [2026-06-24-orchestration-profile-design.md](2026-06-24-orchestration-profile-design.md)

---

本 spec 提出的「公开 `executor.hpp` facade + `EvmExecutor<Profile>` + `ExecutionCoreInput/Output` + TE 双模板」方案，经 grilling 审查后**否决**：

- architecture-review 将其定位为 **P2 候选 8**，优先级低于 ExecutionFrame（候选 1）等 P0 工作
- 引入第三套 Input struct + mapper 与 ADR-019 反 drift 目标冲突
- 与 ExecutionFrame 并行实施 rebase 风险高、收益不确定

**替代方案：** 收窄为具名 `XxxOrchestrationProfile`（Session + `buildHooks`），仅抽取 wrapper 内 hook 策略，不改变对外 API 与 Input/Output 布局。详见 successor spec。

---

<!-- 原文档正文已移除；完整 brainstorming 历史见 git history。 -->
