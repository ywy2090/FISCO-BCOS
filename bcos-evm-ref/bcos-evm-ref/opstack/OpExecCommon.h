#pragma once

#include <evmc/evmc.hpp>
#include <test/state/state.hpp>

namespace bcos::evmref::opstack
{
class OpHost;

struct ExecOutcome
{
    evmc::Result result;
    int64_t gas_used;  // EIP-3529 refund 与 EIP-7623 floor 已结算
};

/// baseline transition() 中段的照抄面（evmone test/state/state.cpp:600-637）：
/// 预热（sender/to/access_list/coinbase@Shanghai+）→ build_message + EIP-7702 委托解析 →
/// host.call → refund = min(delegation+result.gas_refund, used/quotient) → 7623 floor。
/// 前置条件：sender 已 get_or_insert 且 nonce 已递增（CREATE 地址派生用 nonce-1，
/// evmone host.cpp:239）。
ExecOutcome executeMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund);
}  // namespace bcos::evmref::opstack
