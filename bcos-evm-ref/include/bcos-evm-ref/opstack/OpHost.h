#pragma once

#include <bcos-evm-ref/opstack/OpPrecompiles.h>
#include <evmc/evmc.hpp>
#include <test/state/host.hpp>

namespace bcos::evmref::opstack
{
/// evmone::state::Host 子类，修三处缺陷（spec §4.3）：
///  1. get_tx_context：chain_id 用配置值；三 gas 字段=0 时 effective price=0；
///  2. call：命中 PrecompileOverrides（含 0x100）时按 OP precompile 语义派发，未命中回落基类。
///     gas-override（0x100）路径复刻母本 execute_message 派发前语义：全 call-like kind、
///     EVMC_DELEGATED 排除、仅 EVMC_CALL 做 value/touch，失败 rollback。
class OpHost : public evmone::state::Host
{
public:
    OpHost(evmc_revision rev, evmc::VM& vm, evmone::state::State& state,
        const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
        const evmone::state::Transaction& tx, uint64_t chainId,
        const PrecompileOverrides* overrides) noexcept
      : evmone::state::Host{rev, vm, state, block, hashes, tx},
        m_state{state},
        m_chain_id{chainId},
        m_overrides{overrides},
        m_rev{rev},
        m_block{block},
        m_tx{tx}
    {}

    evmc::Result call(const evmc_message& msg) noexcept override;
    [[nodiscard]] evmc_tx_context get_tx_context() const noexcept override;

private:
    evmone::state::State& m_state;
    uint64_t m_chain_id;
    const PrecompileOverrides* m_overrides;
    evmc_revision m_rev;
    const evmone::state::BlockInfo& m_block;
    const evmone::state::Transaction& m_tx;
};
}  // namespace bcos::evmref::opstack
