/*
 * @brief Test hook to spy on applyOpStackMessage → innerExecute path (BCOS_EVM_TESTING).
 * @file ApplyOpStackMessageTestHook.h
 */

#pragma once

#ifdef BCOS_EVM_TESTING

#include "bcos-evm/eth/execution/InnerExecute.h"
#include <functional>
#include <optional>

namespace bcos::evm::opstack::test
{
using ApplyOpStackMessageSpy =
    std::function<std::optional<InnerExecuteOutput>(InnerExecuteInput const&)>;

inline ApplyOpStackMessageSpy& applyOpStackMessageSpySlot()
{
    static ApplyOpStackMessageSpy spy;
    return spy;
}

inline void setApplyOpStackMessageSpy(ApplyOpStackMessageSpy spy)
{
    applyOpStackMessageSpySlot() = std::move(spy);
}

inline void clearApplyOpStackMessageSpy()
{
    applyOpStackMessageSpySlot() = {};
}

inline std::optional<InnerExecuteOutput> maybeCallApplyOpStackMessageSpy(
    InnerExecuteInput const& input)
{
    auto& spy = applyOpStackMessageSpySlot();
    if (spy)
    {
        return spy(input);
    }
    return std::nullopt;
}
}  // namespace bcos::evm::opstack::test

#endif
