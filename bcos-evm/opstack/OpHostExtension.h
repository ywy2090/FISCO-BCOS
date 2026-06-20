#pragma once

#include "bcos-evm/eth/policy/HostExtension.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/L1BlockPredeploy.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include <cstring>

namespace bcos::evm
{
class OpHostExtension final : public state::HostExtension
{
public:
    explicit OpHostExtension(state::State* state = nullptr) : m_state(state) {}

    void prepareMessage(evmc_revision /*rev*/, evmc_message& /*msg*/) override {}
    void setCallerAddress(const evmc_address& /*caller*/) override {}
    void bumpContractCreateNonce(const evmc_address& /*contractAddress*/) override {}

    std::optional<evmc_result> tryChainPrecompile(
        evmc_revision /*rev*/, const evmc_message& msg) override
    {
        auto const target = msg.code_address;
        if (std::memcmp(target.bytes, OP_L1_BLOCK_PREDEPLOY.bytes, sizeof(target.bytes)) == 0)
        {
            if (m_state == nullptr)
            {
                return std::nullopt;
            }
            return L1BlockPredeploy::dispatch(*m_state, msg);
        }
        return std::nullopt;
    }

private:
    state::State* m_state{nullptr};
};
}  // namespace bcos::evm
