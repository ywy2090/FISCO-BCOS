#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoChainCallTargetClassify.h"

namespace bcos::evm
{

std::optional<execution::CallTargetDescriptor> FiscoChainCallTargetAdapter::classifyTarget(
    state::State& state, evmc_address const& executionAddress, evmc_message const& /*msg*/,
    execution::FrameScope /*scope*/)
{
    auto const dispatchAddress = resolveFiscoChainDispatchAddress(state, executionAddress);
    if (!dispatchAddress.has_value())
    {
        return std::nullopt;
    }

    return execution::CallTargetDescriptor{
        .kind = execution::CallTargetKind::ChainPrecompile,
        .dispatchAddress = *dispatchAddress,
        .warmPolicy = execution::WarmPolicy::Never,
    };
}

std::optional<evmc_result> FiscoChainCallTargetAdapter::dispatch(
    evmc_revision rev, evmc_message const& msg)
{
    auto routed = routeFiscoChainPrecompileMessage(&m_state, msg);
    if (!routed.has_value())
    {
        return std::nullopt;
    }
    return m_dispatchPort.dispatch(rev, *routed);
}

void FiscoChainCallTargetAdapter::forEachStaticWarmTarget(
    std::function<void(evmc_address const&)> const& /*consume*/) const
{
    // FISCO has no static tx-entry warm targets (dynamic [PRECOMPILED] uses classifyTarget only).
}

}  // namespace bcos::evm
