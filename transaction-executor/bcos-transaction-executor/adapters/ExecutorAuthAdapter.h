#pragma once

#include "ExecutorSessionContext.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "transaction-executor/bcos-transaction-executor/adapters/AuthCheck.h"
#include <bcos-task/Wait.h>
#include <optional>

namespace bcos::transaction_executor
{

template <class SessionContext>
class ExecutorAuthAdapter final : public evm::AuthPort
{
public:
    explicit ExecutorAuthAdapter(SessionContext& sessionContext) : m_sessionContext(sessionContext)
    {}

    std::optional<evm::EVMCResult> checkAuth(evmc_message const& msg) override
    {
        return evm::checkAuth(m_sessionContext.storage, m_sessionContext.blockHeader, msg,
            m_sessionContext.origin, m_sessionContext.precompiledManager,
            m_sessionContext.contextID, m_sessionContext.seq, m_sessionContext.hashImpl,
            m_sessionContext.revisionConfig.fix_auth_check);
    }

    void createAuthTable(evmc_message const& msg, std::string_view tablePath) override
    {
        task::syncWait(evm::createAuthTable(m_sessionContext.storage, m_sessionContext.blockHeader,
            msg, m_sessionContext.origin, tablePath, evm::noOpExternalCaller(),
            m_sessionContext.precompiledManager, m_sessionContext.contextID, m_sessionContext.seq,
            m_sessionContext.ledgerConfig));
    }

private:
    SessionContext& m_sessionContext;
};

template <class SessionContext>
ExecutorAuthAdapter(SessionContext&) -> ExecutorAuthAdapter<SessionContext>;

}  // namespace bcos::transaction_executor
