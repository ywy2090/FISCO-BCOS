#pragma once

#include "FiscoPortAdapterContext.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "transaction-executor/bcos-transaction-executor/adapters/AuthCheck.h"
#include <bcos-task/Wait.h>
#include <optional>

namespace bcos::transaction_executor
{

template <class PortAdapterContext>
class ExecutorAuthAdapter final : public evm::AuthPort
{
public:
    explicit ExecutorAuthAdapter(PortAdapterContext& portAdapterContext)
      : m_portAdapterContext(portAdapterContext)
    {}

    std::optional<evm::EVMCResult> checkAuth(evmc_message const& msg) override
    {
        return evm::checkAuth(m_portAdapterContext.storage, m_portAdapterContext.blockHeader, msg,
            m_portAdapterContext.origin, m_portAdapterContext.precompiledManager,
            m_portAdapterContext.contextID, m_portAdapterContext.seq, m_portAdapterContext.hashImpl,
            m_portAdapterContext.revisionConfig.fix_auth_check);
    }

    void createAuthTable(evmc_message const& msg, std::string_view tablePath) override
    {
        task::syncWait(
            evm::createAuthTable(m_portAdapterContext.storage, m_portAdapterContext.blockHeader,
                msg, m_portAdapterContext.origin, tablePath, evm::noOpExternalCaller(),
                m_portAdapterContext.precompiledManager, m_portAdapterContext.contextID,
                m_portAdapterContext.seq, m_portAdapterContext.ledgerConfig));
    }

private:
    PortAdapterContext& m_portAdapterContext;
};

template <class PortAdapterContext>
ExecutorAuthAdapter(PortAdapterContext&) -> ExecutorAuthAdapter<PortAdapterContext>;

}  // namespace bcos::transaction_executor
