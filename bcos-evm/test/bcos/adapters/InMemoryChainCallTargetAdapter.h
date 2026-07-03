#pragma once

#include "bcos-evm/eth/core/CallTargetTypes.h"
#include "bcos-evm/eth/core/ChainCallTargetPort.h"
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm::test
{

class InMemoryChainCallTargetAdapter final : public ChainCallTargetPort
{
public:
    using ClassifyFn = std::function<std::optional<execution::ClassifiedCallTarget>(
        state::State&, evmc_address const&, evmc_message const&, execution::FrameScope)>;
    using DispatchFn =
        std::function<std::optional<evmc_result>(evmc_revision, evmc_message const&)>;

    explicit InMemoryChainCallTargetAdapter(ClassifyFn classify, DispatchFn dispatch = {})
      : m_classify(std::move(classify)), m_dispatch(std::move(dispatch))
    {}

    void addStaticWarmTarget(evmc_address const& addr) { m_staticWarm.push_back(addr); }

    std::optional<execution::ClassifiedCallTarget> classifyTarget(state::State& s,
        evmc_address const& a, evmc_message const& m, execution::FrameScope scope) override
    {
        return m_classify ? m_classify(s, a, m, scope) : std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision r, evmc_message const& m) override
    {
        return m_dispatch ? m_dispatch(r, m) : std::nullopt;
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const& c) const override
    {
        for (auto const& a : m_staticWarm)
        {
            c(a);
        }
    }

private:
    ClassifyFn m_classify;
    DispatchFn m_dispatch;
    std::vector<evmc_address> m_staticWarm;
};

}  // namespace bcos::evm::test
