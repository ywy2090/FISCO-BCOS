// bcos-evm-ref/adapter/StateDiffWriteback.h
#pragma once

#include <test/utils/test_state.hpp>

namespace bcos::evmref
{
/// v1 写回缝：把 evmone StateDiff 应用到内存 TestState。
/// 契约（真账本写回实现必须满足，见 StateDiffWritebackTest 缝契约用例）：
///   1) deleted_accounts 必须删除（Cancun 后并非恒空：EIP-6780 同交易自毁、
///      EIP-161 空账户擦除都会产生删除项；state_diff.hpp 的"恒空"注释已过时）；
///   2) modified_storage 值为 0 表示删除槽（erase），不是存零；
///   3) code 仅在 has_value() 时覆盖。
inline void applyStateDiff(evmone::test::TestState& state, const evmone::state::StateDiff& diff)
{
    state.apply(diff);
}
}  // namespace bcos::evmref
