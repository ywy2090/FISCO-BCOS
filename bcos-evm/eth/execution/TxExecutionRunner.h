/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Top-level tx execution adapter (warm / 7702 / frame / finalize).
 * @file TxExecutionRunner.h
 */

#pragma once

#include "bcos-evm/eth/InnerExecute.h"

namespace bcos::evm::execution
{

/// Tx-level story between stateTransitionExecute and runCallFrame (PR-A deep module).
struct TxExecutionRunner
{
    static InnerExecuteOutput runEvmKernelTopLevel(InnerExecuteInput input);
};

}  // namespace bcos::evm::execution
