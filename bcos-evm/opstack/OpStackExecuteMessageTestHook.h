#pragma once

#ifdef BCOS_EVM_TESTING

#include "bcos-evm/eth/InnerExecute.h"
#include <functional>
#include <optional>

namespace bcos::evm::opstack::test
{
using ExecuteMessageSpy =
    std::function<std::optional<InnerExecuteOutput>(InnerExecuteInput const&)>;

inline ExecuteMessageSpy& executeMessageSpySlot()
{
    static ExecuteMessageSpy spy;
    return spy;
}

inline void setExecuteMessageSpy(ExecuteMessageSpy spy)
{
    executeMessageSpySlot() = std::move(spy);
}

inline void clearExecuteMessageSpy()
{
    executeMessageSpySlot() = {};
}

inline std::optional<InnerExecuteOutput> maybeCallExecuteMessageSpy(InnerExecuteInput const& input)
{
    auto& spy = executeMessageSpySlot();
    if (spy)
    {
        return spy(input);
    }
    return std::nullopt;
}
}  // namespace bcos::evm::opstack::test

#endif
