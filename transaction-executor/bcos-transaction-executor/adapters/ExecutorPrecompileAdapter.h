#pragma once

#include "ExecutorSessionContext.h"
#include "bcos-evm/eth/ports/ChainCallTargetPort.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h"
#include <optional>

namespace bcos::transaction_executor
{

template <class SessionContext>
class ExecutorPrecompileAdapter final : public evm::ChainCallTargetPort
{
public:
    explicit ExecutorPrecompileAdapter(SessionContext& sessionContext)
      : m_sessionContext(sessionContext)
    {}

    std::optional<bcos::evm::execution::CallTargetDescriptor> classifyTarget(
        bcos::evm::state::State&, evmc_address const&, evmc_message const&,
        bcos::evm::execution::FrameScope) override
    {
        return std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override
    {
        auto const* precompiled = m_sessionContext.precompiledManager.getPrecompiled(msg.recipient,
            m_sessionContext.revisionConfig, m_sessionContext.ledgerConfig.features());
        if (precompiled == nullptr)
        {
            return std::nullopt;
        }

        auto result = evm::callPrecompiled(*precompiled, m_sessionContext.storage,
            m_sessionContext.blockHeader, msg, m_sessionContext.origin, evm::noOpExternalCaller(),
            m_sessionContext.precompiledManager, m_sessionContext.contextID, m_sessionContext.seq,
            m_sessionContext.revisionConfig.enable_auth_check,
            m_sessionContext.revisionConfig.eth(), rev,
            m_sessionContext.revisionConfig.fix_error_handling);
        evmc_result raw = result;
        result.output_data = nullptr;
        result.output_size = 0;
        result.release = nullptr;
        return raw;
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const&) const override {}

private:
    SessionContext& m_sessionContext;
};

template <class SessionContext>
ExecutorPrecompileAdapter(SessionContext&) -> ExecutorPrecompileAdapter<SessionContext>;

}  // namespace bcos::transaction_executor
