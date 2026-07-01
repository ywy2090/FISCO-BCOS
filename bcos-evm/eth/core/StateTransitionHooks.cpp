/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file StateTransitionHooks.cpp
 */

#include "bcos-evm/eth/core/StateTransitionHooks.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"

namespace bcos::evm
{

void StateTransitionHooks::onNormalizeMessage(StateTransitionContext& ctx) const
{
    (void)ctx;
}

void StateTransitionHooks::onPreCheckRules(StateTransitionContext& ctx) const
{
    (void)ctx;
}

void StateTransitionHooks::onPreCheckGasAffordable(StateTransitionContext& ctx) const
{
    (void)ctx;
}

void StateTransitionHooks::onPreCheckCanTransfer(StateTransitionContext& ctx) const
{
    (void)ctx;
}

void StateTransitionHooks::onTuneInnerExecuteInput(InnerExecuteInput& input) const
{
    (void)input;
}

InnerExecuteOutput StateTransitionHooks::onInvokeInnerExecute(InnerExecuteInput&& input) const
{
    return innerExecute(std::move(input));
}

}  // namespace bcos::evm
