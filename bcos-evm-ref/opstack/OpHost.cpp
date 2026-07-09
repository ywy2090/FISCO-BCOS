#include <bcos-evm-ref/opstack/OpHost.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <test/state/precompiles.hpp>
#include <test/state/precompiles_internal.hpp>

namespace bcos::evmref::opstack
{
namespace
{
evmc::Result executeGasOverridePrecompile(const evmc_message& msg, int64_t gas_cost)
{
    if (msg.gas < gas_cost)
        return evmc::Result{EVMC_OUT_OF_GAS, 0};

    const int64_t gas_left = msg.gas - gas_cost;

    constexpr size_t kMaxOutput = 32;
    std::array<uint8_t, kMaxOutput> output{};
    const auto exec = evmone::state::p256verify_execute(
        msg.input_data, msg.input_size, output.data(), output.size());

    if (exec.status_code == EVMC_SUCCESS && exec.output_size > 0)
        return evmc::Result{EVMC_SUCCESS, gas_left, 0, output.data(), exec.output_size};

    return evmc::Result{exec.status_code, gas_left};
}
}  // namespace

evmc_tx_context OpHost::get_tx_context() const noexcept
{
    const auto base_fee =
        (m_rev >= EVMC_LONDON) ? intx::uint256{m_block.base_fee} : intx::uint256{0};

    intx::uint256 effective_gas_price;
    if (m_tx.max_gas_price == 0)
    {
        effective_gas_price = 0;
    }
    else
    {
        const auto priority_gas_price =
            std::min(m_tx.max_priority_gas_price, m_tx.max_gas_price - base_fee);
        effective_gas_price = base_fee + priority_gas_price;
    }

    return evmc_tx_context{
        intx::be::store<evmc::uint256be>(effective_gas_price),
        m_tx.sender,
        m_block.coinbase,
        m_block.number,
        m_block.timestamp,
        m_block.gas_limit,
        m_block.prev_randao,
        intx::be::store<evmc::uint256be>(intx::uint256{m_chain_id}),
        intx::be::store<evmc::uint256be>(intx::uint256{m_block.base_fee}),
        intx::be::store<evmc::uint256be>(m_block.blob_base_fee.value_or(0)),
        m_tx.blob_hashes.data(),
        m_tx.blob_hashes.size(),
        nullptr,
        0,
    };
}

evmc::Result OpHost::call(const evmc_message& msg) noexcept
{
    if (m_overrides != nullptr && msg.kind == EVMC_CALL)
    {
        if (const auto* entry = m_overrides->find(msg.code_address); entry != nullptr)
        {
            if (entry->max_input_size > 0 &&
                static_cast<size_t>(msg.input_size) > entry->max_input_size)
                return evmc::Result{EVMC_FAILURE, 0};

            if (entry->gas_cost_override >= 0)
                return executeGasOverridePrecompile(msg, entry->gas_cost_override);

            return evmone::state::Host::call(msg);
        }
    }
    return evmone::state::Host::call(msg);
}
}  // namespace bcos::evmref::opstack
