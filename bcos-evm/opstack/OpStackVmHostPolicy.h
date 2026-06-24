#pragma once

#include "bcos-evm/eth/policy/VmHostPolicy.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/l1/GasPriceOraclePredeploy.h"
#include "bcos-evm/opstack/l1/L1BlockPredeploy.h"
#include <bcos-utilities/Common.h>
#include <cstring>

namespace bcos::evm
{
class OpStackVmHostPolicy final : public state::VmHostPolicy
{
public:
    explicit OpStackVmHostPolicy(state::State* state = nullptr, bcos::u256 l2BaseFee = 0)
      : m_state(state), m_l2BaseFee(std::move(l2BaseFee))
    {}

    void prepareMessage(evmc_revision /*rev*/, evmc_message& /*msg*/) override {}
    void setCallerAddress(const evmc_address& /*caller*/) override {}
    void bumpContractCreateNonce(const evmc_address& /*contractAddress*/) override {}

    std::optional<evmc_result> tryChainPrecompile(
        evmc_revision /*rev*/, const evmc_message& msg) override
    {
        if (m_state == nullptr)
        {
            return std::nullopt;
        }

        auto const target = msg.code_address;
        if (sameAddress(target, OP_L1_BLOCK_PREDEPLOY))
        {
            return L1BlockPredeploy::dispatch(*m_state, msg);
        }
        if (sameAddress(target, OP_GAS_PRICE_ORACLE_PREDEPLOY))
        {
            return GasPriceOraclePredeploy::dispatch(*m_state, msg, m_l2BaseFee);
        }
        return std::nullopt;
    }

private:
    static bool sameAddress(evmc_address const& left, evmc_address const& right) noexcept
    {
        return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
    }

    state::State* m_state{nullptr};
    bcos::u256 m_l2BaseFee{0};
};
}  // namespace bcos::evm
