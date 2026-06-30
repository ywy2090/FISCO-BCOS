#pragma once

#include "bcos-evm/eth/core/ChainExtendedPrecompileDispatch.h"
#include <functional>
#include <optional>

namespace bcos::evm::test
{

/// Test double: dispatch-only `ChainExtendedPrecompileDispatch` (classification via
/// `FiscoChainCallTargetAdapter`).
class InMemoryChainPrecompileAdapter final : public ChainExtendedPrecompileDispatch
{
public:
    using Handler = std::function<std::optional<evmc_result>(evmc_revision, evmc_message const&)>;

    explicit InMemoryChainPrecompileAdapter(Handler handler = {}) : m_handler(std::move(handler)) {}

    std::optional<execution::CallTargetDescriptor> classifyTarget(
        state::State&, evmc_address const&, evmc_message const&, execution::FrameScope) override
    {
        return std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) override
    {
        if (!m_handler)
        {
            return std::nullopt;
        }
        return m_handler(rev, msg);
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const&) const override {}

private:
    Handler m_handler;
};

}  // namespace bcos::evm::test
