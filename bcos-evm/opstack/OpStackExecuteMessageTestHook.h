#pragma once

#ifdef BCOS_EVM_TESTING

#include "bcos-evm/eth/ExecuteMessage.h"
#include <functional>
#include <optional>

namespace bcos::evm::opstack::test
{
using ExecuteMessageSpy =
    std::function<std::optional<ExecuteMessageOutput>(ExecuteMessageInput const&)>;

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

inline std::optional<ExecuteMessageOutput> maybeCallExecuteMessageSpy(
    ExecuteMessageInput const& input)
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
