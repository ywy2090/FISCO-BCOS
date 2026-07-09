// bcos-evm-ref/include/bcos-evm-ref/adapter/StateViewAdapter.h
#pragma once

#include <test/state/state_view.hpp>

namespace bcos::evmref
{
/// v1 占位（spec §3.1/§4.1，BlockHashesAdapter 合并于此，见"声明偏差"节）：
/// 测试后端直接用 evmone::test::TestState。
/// 真账本桥接时在此实现 evmone::state::StateView 的三个只读方法；
/// 注意 StateView 是同步 noexcept 接口且 get_account_code 按值返回整段代码，
/// 桥接协程账本的性能评估见 spec §7.1。
using StateView = evmone::state::StateView;
using BlockHashes = evmone::state::BlockHashes;
}  // namespace bcos::evmref
