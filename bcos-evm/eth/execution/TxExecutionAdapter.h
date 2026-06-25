/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Top-level tx execution adapter (warm / 7702 / frame / finalize).
 * @file TxExecutionAdapter.h
 */

#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"

namespace bcos::evm::execution
{

/// Tx-level story between runTxPipeline and runExecutionFrame (PR-A deep module).
struct TxExecutionAdapter
{
    static ExecuteMessageOutput run(ExecuteMessageInput input);
};

}  // namespace bcos::evm::execution
