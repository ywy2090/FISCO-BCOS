#pragma once

#include "bcos-evm/eth/core/ChainCallTargetDispatcher.h"

namespace bcos::evm
{

class FiscoChainCallTargetAdapter final : public ChainCallTargetDispatcher
{
public:
    FiscoChainCallTargetAdapter(state::State& state, ChainCallTargetDispatcher& dispatchPort)
      : m_state(state), m_dispatchPort(dispatchPort)
    {}

    std::optional<execution::CallTargetDescriptor> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) override;

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override;

    void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const override;

private:
    state::State& m_state;
    ChainCallTargetDispatcher& m_dispatchPort;
};

}  // namespace bcos::evm
