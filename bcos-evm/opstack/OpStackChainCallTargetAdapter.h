#pragma once

#include "bcos-evm/eth/ports/ChainCallTargetPort.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include <bcos-utilities/Common.h>
#include <functional>

namespace bcos::evm
{

class OpStackChainCallTargetAdapter final : public ChainCallTargetPort
{
public:
    OpStackChainCallTargetAdapter(state::State* state, bcos::u256 l2BaseFee,
        OpStackForkSchedule forkSchedule, uint64_t blockTimestamp);

    std::optional<execution::CallTargetDescriptor> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) override;

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override;

    void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const override;

private:
    state::State* m_state{nullptr};
    bcos::u256 m_l2BaseFee{0};
    OpStackForkSchedule m_forkSchedule{makeIsthmusPlusForkSchedule()};
    uint64_t m_blockTimestamp{0};
};

}  // namespace bcos::evm
