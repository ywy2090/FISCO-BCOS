/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Top-level tx execution adapter (warm / 7702 / frame / finalize).
 * @file TxExecutionRunner.h
 */

#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"

namespace bcos::evm::execution
{

/// Tx-level story between runTxPipeline and runCallFrame (PR-A deep module).
struct TxExecutionRunner
{
    static ExecuteMessageOutput runEvmKernelTopLevel(ExecuteMessageInput input);

    [[deprecated("Use runEvmKernelTopLevel")]] static ExecuteMessageOutput run(
        ExecuteMessageInput input)
    {
        return runEvmKernelTopLevel(std::move(input));
    }
};

}  // namespace bcos::evm::execution
