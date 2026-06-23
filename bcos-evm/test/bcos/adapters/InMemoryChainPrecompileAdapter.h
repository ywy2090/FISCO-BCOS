#pragma once

#include "bcos-evm/bcos/ports/ChainPrecompilePort.h"
#include <functional>
#include <optional>

namespace bcos::evm::test
{

class InMemoryChainPrecompileAdapter final : public ChainPrecompilePort
{
public:
    using Handler = std::function<std::optional<evmc_result>(evmc_revision, evmc_message const&)>;

    explicit InMemoryChainPrecompileAdapter(Handler handler = {}) : m_handler(std::move(handler)) {}

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override
    {
        if (!m_handler)
        {
            return std::nullopt;
        }
        return m_handler(rev, msg);
    }

private:
    Handler m_handler;
};

}  // namespace bcos::evm::test
