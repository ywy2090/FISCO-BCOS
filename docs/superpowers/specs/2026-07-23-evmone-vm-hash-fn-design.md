# evmone SM3 patch 瘦身:hash_fn 由 VM 对象携带

- 日期:2026-07-23
- 状态:已评审(设计经代码对齐审查修订)
- 前置:PR #5351(官方 ipsilon/evmone + fisco-sm3.patch)合入
- 落地:基于 `evmone-official-dep` 的独立后续 PR(分支 `refactor-evmone-vm-hash-fn`)

## 1. 背景与动机

PR #5351 将 evmone 依赖从私有 fork 切到官方 ipsilon/evmone v0.21.0,SM3 国密支持以
`ports/evmone/fisco-sm3.patch`(~500 行)形式重新施加。patch 中约六成内容
(evmc.h 将 `evmc_host_context` 具体化、evmc.hpp 的 WrappedHostContext/magic tag/
15 个 internal:: 回调改写)只为解决一个问题:**hash_fn 如何搭乘 opaque 的
context 指针**,以同时服务三条调用路径:

1. 旧执行器 C-ABI:`VMFactory.cpp:44` → `evmc::VM::execute(interface, ctx, ...)`
2. 两执行器直调:`evmone::baseline::execute(vm, interface, ctx, ...)`
3. bcos-evm(以太坊兼容层):`evmc::Host` C++ API(`host.cpp:291/374`)

这部分是对 evmc 头文件的 ABI 级 fork,是未来升级 evmone 的最大维护负担。

**审查发现的关键事实**(本设计的直接依据):两个执行器的 `evm_hash_fn`
**均忽略 context 参数**,散列分发本来就是进程全局的:

```cpp
// bcos-executor/src/vm/HostContext.cpp:59
evm_hash_fn(evmc_host_context* /*context*/, ...)
{ return toEvmC(HostContext::hashImpl()->hash(...)); }   // → GlobalHashImpl::g_hashImpl

// transaction-executor/.../vm/HostContext.cpp:7
evm_hash_fn(evmc_host_context* /*context*/, ...)
{ return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(...)); }
```

即:现有 per-context 携带机制运送的是一个不看 context 的函数——过度设计。
hash_fn 挂到 **VM 对象**上即可覆盖全部路径。

## 2. 目标

- `fisco-sm3.patch` 从 ~500 行缩到 ~150 行
- `evmc/` 头文件(evmc.h / evmc.hpp / mocked_host.hpp)**零改动**,
  `evmc_host_context` 恢复上游 opaque 声明
- 删除 WrappedHostContext、magic tag、internal:: 回调改写、VM::execute 出线定义
- SM3 语义与 #5351 逐字节一致

## 3. 行为不变量(验收标准)

1. SM 链:KECCAK256 opcode → SM3(经 `evm_hash_fn` → `g_hashImpl`),不变
2. 非 SM 链:host hash 仍被设置(今天亦如此,`evm_hash_fn` 内部分发到 keccak
   hashImpl),不变
3. bcos-evm:不设 hash_fn → nullptr → 上游 `ethash::keccak256`
   (与今天 WrappedHostContext 携带 null 的回退等价);**SM 节点上 bcos-evm
   仍是 keccak** —— 这是 per-VM 隔离的硬要求(见 §7 变体 3 否决理由)
4. patch 中 noexcept 移除与 `-fno-exceptions` 移除**保留,定位为 fork parity**。
   (评审更正:此前「异常穿透 evmone 传播刚需」的论断不成立——回调与 execute
   之间各帧(`evmc.hpp` HostContext 包装、`call_impl`、`invoke`/`dispatch`)仍是
   noexcept,逃逸异常一律 `std::terminate`;`NotEnoughCashError` 等实际在回调
   **内部**被捕获,从不穿越 evmone。恢复 noexcept + 回调 catch-all 属独立行为
   实验,不在本次范围。)
5. `evmc_message` 保持上游布局(#5351 已达成)

## 4. 数据流

```
FISCO 创建 VM ─► static_cast<evmone::VM*>(vm.get_raw_pointer())->hash_fn = evm_hash_fn
                     │            (cast 模式 VMInstance.cpp:79 已在用)
   执行(任意入口:C-ABI evmc_execute_fn / 直调 baseline::execute / advanced)
                     │
   ExecutionState 捕获 { vm.hash_fn, host_ctx }
                     │  (evmone 0.21 state 池本就在 VM 内:vm.hpp
                     │   m_execution_states + get_execution_state(depth),
                     │   且已有 public 成员先例 cgoto —— 加成员合乎其风格)
   KECCAK256 指令: state.hash_fn ? state.hash_fn(ctx, data, size)
                                  : ethash::keccak256
```

hash_fn 回调签名保持 EVMC 风格(首参 `evmc_host_context*`),ExecutionState 调用
时传入其持有的 host ctx;当前两实现均忽略该参数(§1),保留签名仅为一般性。

## 5. patch 侧改动(新 fisco-sm3.patch 构成)

| 动作 | 文件 | 内容 |
|---|---|---|
| 新增 | `lib/evmone/vm.hpp` | `VM` 加 `evmc_bytes32 (*hash_fn)(evmc_host_context*, const uint8_t*, size_t) = nullptr;` |
| 新增 | `lib/evmone/execution_state.hpp` | +2 成员(hash_fn、host ctx),ctor/reset 捕获 |
| 新增 | `baseline_execution.cpp` / `advanced_execution.cpp` | 把 `vm.hash_fn` 线程化进 state |
| 保留 | `lib/evmone/instructions.hpp` | KECCAK256 判空路由(与 #5351 相同) |
| 保留 | noexcept 移除 + `-fno-exceptions` 移除 | 异常传播刚需(§3.4) |
| 保留 | 构建打包 | macOS `libtool -static`、内部头安装、cmake config |
| **删除** | `evmc/include/evmc/evmc.h` | 具体化整段(顺带消除 `interface` 字段名与 Windows SDK `#define interface struct` 宏的冲突) |
| **删除** | `evmc/include/evmc/evmc.hpp` | WrappedHostContext/magic/internal:: 改写/set·get_hash_fn/VM::execute 出线 |
| **删除** | `evmc/include/evmc/mocked_host.hpp` | hash 扩展(审查确认:全仓无使用者) |
| **删除** | `lib/evmone/CMakeLists.txt` 的 cmake config 生成块 | 评审发现死链:其产物被 portfile 无条件 `REMOVE_RECURSE`,且 portfile step 1b 的 `vcpkg_replace_string` 因转义 bug 从未匹配过 |
| 卫生 | noexcept 剥离残留的尾随空格/`) ;`(5 处) | 重新生成时清除 |

`ports/evmone/vcpkg.json` port-version 5→6。**portfile.cmake 一并清理**(评审发现):
删除 step 1b(转义 bug 静默 no-op)、step 2(官方源无 export set,死逻辑)、
step 4b(与 patch 的 DIRECTORY 安装完全冗余);`evmone::precompiles` 目标补
`IMPORTED_LOCATION_DEBUG`(缺失导致 Debug 消费者链 release 库,MSVC 上 LNK2038)。

## 6. FISCO 侧改动(~8 文件,测试 ~0)

1. **bcos-executor `HostContext`**:去掉 `: public evmc_host_context`;
   接口表改普通成员并**更名 `hostInterface`**(避开 Windows SDK
   `#define interface struct` 宏,评审建议);删 ctor 里 `hash_fn = evm_hash_fn;`
2. **bcos-executor `EVMHostInterface.cpp`**:**16 处**
   `static_cast<HostContext&>(*context)` → `reinterpret_cast`(机械替换;
   前提:这些路径的 ctx 恒为对应执行器的 HostContext,见 §8)
3. **bcos-executor `VMInstance`/`VMFactory`**:两条路径
   (C-ABI ctor `VMFactory.cpp:44`、fresh-VM `VMInstance.cpp:78`)创建 VM 后设
   `vm->hash_fn = evm_hash_fn`(无条件,与今天一致);
   `VMInstance::execute` 读 interface 改经访问器
4. **transaction-executor `HostContext.h`**:删基类与聚合初始化
   (`:207`),接口表改私有成员 `m_hostInterface`(贴合该类 m_ 前缀惯例;
   更名理由同项 1);`:885` 传 `reinterpret_cast<evmc_host_context*>(this)`
5. **transaction-executor `EVMHostInterface.h`**:**16 处**
   `static_cast<HostContextType&>` → `reinterpret_cast`
6. **transaction-executor `VMInstance.cpp:20`**:fresh VM 设 `vm->hash_fn`
7. **bcos-evm:零改动**
8. **测试:现有测试零改动**(审查确认:全仓无直接聚合构造 `evmc_host_context`
   的测试,无 MockedHost hash 使用者);**新增 1 个 seam 级测试**
   `bcos-executor/test/unittest/evmone/HashFnRoutingTest.cpp`:直接构造 evmone
   VM,断言(a)设 sentinel hash_fn 后 KECCAK256 opcode 输出 == sentinel,
   (b)不设时输出 == keccak256("") 常量——补上评审指出的缺口(fork 曾有的
   evmone 级 hook 测试被丢,且 FISCO 现有测试无 opcode 级 hash 路由断言)

## 7. 已否决的替代方案

- **set_option("hash_fn", "0x…")**:标准 C API,但指针序列化成字符串、零类型
  检查,patch 量不减。否。
- **evmone 进程级全局 hash fn**:改动最小,但会让 **bcos-evm 的 VM 也被注入**
  → SM 节点上以太坊兼容层的 KECCAK256 变成 SM3,错误。必须 per-VM 隔离。否。
  (注:否决理由不是「与混跑测试冲突」——SM/keccak 的选择在调用时经
  `g_hashImpl` 全局完成,测试换全局即可;真正的阻断是 bcos-evm 隔离。)
- **全面统一到 evmc::Host 虚函数**:概念最干净,但需重写两执行器约 20 个回调,
  热路径高风险,收益比 VM 方案增量有限。留作远期,不在本次范围。
- **链接期替换 `ethash::keccak256` 符号**:ODR/链接顺序脆弱、波及面失控。否。

## 8. 风险与对策

| 风险 | 对策 |
|---|---|
| ABI 幽灵:struct 布局变化 + 增量构建 = 用旧头编译的 .o 造成假失败(本项目已实际踩过) | port-version bump 强制 evmone 重建;FISCO 侧**全量重编后**才可判定测试结果 |
| `reinterpret_cast` 前提被破坏(某处向 execute 传了非 HostContext 的 ctx) | 审查确认现状只有两执行器各自的 ctx 传入自身 shims;在两个 EVMHostInterface 顶部加注释声明该前提 |
| 热路径类型结构变化引入回归 | §9 全量验证;proxyReceive 判定口径见 §9 注 |
| vm.hpp 内部头耦合 | 现状已耦合(`baseline::execute` 直调 + static_cast),不新增 |

## 9. 验证计划

1. 干净全量重编(先证 patch 应用、安装头正确:检查安装的 evmc.h 恢复 opaque)
2. bcos-executor 全套件 351 用例(含 SM3、EVM keccak/sha3、V3.1 precompiled
   gas 边界)
3. transaction-executor:全套件 + `proxyReceive` **隔离运行**
   (注:proxyReceive 在完整套件中的失败是 pre-existing 进程内顺序依赖,
   base 分支同样失败;判定回归必须同口径——隔离对隔离、ctest 对 ctest)
4. bcos-evm EthTransitionTest(验证不变量 §3.3)
5. **HashFnRoutingTest**(§6.8 新增):sentinel 路由 + keccak 回退两断言通过
6. ctest 全量:仅允许已知 pre-existing 失败(TiKV 需 live server、test-rpbft、
   WsToolsTest)

## 10. 规模估计与交付

- patch:~500 → ~150 行(evmc 头零改动)
- FISCO 生产代码:~8 文件;测试:+1 新文件(现有零改动)
- 分支:`refactor-evmone-vm-hash-fn`(基于 `evmone-official-dep`);
  #5351 合入后提独立 PR,commit 拆分建议:
  1. patch 重写 + port-version bump + portfile 清理(evmone 侧)
  2. 两执行器 de-inherit + cast 扫 + VM 设置点 + 新测试(FISCO 侧)

## 11. 评审修订记录(2026-07-23,4-agent 审查)

本 spec 依据对 #5351 的四视角审查(patch 语义/port 构建/FISCO 侧/共识等价)修订:

- **共识等价获独立确证**:fork 源与官方 v0.21.0 逐字节比较,`lib/evmone/` 与
  `evmc.hpp` 在 fork 与官方+patch 间完全一致;全部差异即死字段删除。
- **§3.4 更正**:「noexcept 移除=异常传播刚需」被证伪,改为 fork parity 定位。
- **本设计消除的评审 Major**:`internal::get_hash_fn` 兜底盲读 offset 8 的
  type-pun 地雷(对下游安装头是隐患)——恢复 opaque 后整体消失。
- **纳入范围**:portfile 死逻辑清理(step 1b/2/4b)、precompiles Debug 位置、
  patch 卫生、`interface` 更名、HashFnRoutingTest。
- **明确不纳入**:回调 no-throw 契约整治(v1 `EVMHostInterface.h:225` 按不存在
  的传播契约抛 `GasOverflow`,触发即 terminate——fork parity 的 latent 问题,
  留独立 PR)。
