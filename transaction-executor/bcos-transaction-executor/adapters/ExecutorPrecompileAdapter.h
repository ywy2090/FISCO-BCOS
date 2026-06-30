#pragma once

#include "FiscoPortAdapterContext.h"
#include "bcos-evm/eth/core/ChainPrecompileDispatch.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h"
#include <optional>

namespace bcos::transaction_executor
{

template <class PortAdapterContext>
class ExecutorPrecompileAdapter final : public evm::ChainPrecompileDispatch
{
public:
    explicit ExecutorPrecompileAdapter(PortAdapterContext& portAdapterContext)
      : m_portAdapterContext(portAdapterContext)
    {}

    std::optional<bcos::evm::execution::CallTargetDescriptor> classifyTarget(
        bcos::evm::state::State&, evmc_address const&, evmc_message const&,
        bcos::evm::execution::FrameScope) override
    {
        return std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override
    {
        auto const* precompiled =
            m_portAdapterContext.precompiledManager.getPrecompiled(msg.recipient,
                m_portAdapterContext.revisionConfig, m_portAdapterContext.ledgerConfig.features());
        if (precompiled == nullptr)
        {
            return std::nullopt;
        }

        auto result = evm::callPrecompiled(*precompiled, m_portAdapterContext.storage,
            m_portAdapterContext.blockHeader, msg, m_portAdapterContext.origin,
            evm::noOpExternalCaller(), m_portAdapterContext.precompiledManager,
            m_portAdapterContext.contextID, m_portAdapterContext.seq,
            m_portAdapterContext.revisionConfig.enable_auth_check,
            m_portAdapterContext.revisionConfig.eth(), rev,
            m_portAdapterContext.revisionConfig.fix_error_handling);
        evmc_result raw = result;
        result.output_data = nullptr;
        result.output_size = 0;
        result.release = nullptr;
        return raw;
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const&) const override {}

private:
    PortAdapterContext& m_portAdapterContext;
};

template <class PortAdapterContext>
ExecutorPrecompileAdapter(PortAdapterContext&) -> ExecutorPrecompileAdapter<PortAdapterContext>;

}  // namespace bcos::transaction_executor
