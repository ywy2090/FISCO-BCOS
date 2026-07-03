/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execution gas metering for Eth normal txs (Task 4 extends this file).
 * @file EthTxFinalize.h
 */

#pragma once

namespace bcos::evm
{

struct EthTxFinalizeResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
};

}  // namespace bcos::evm
