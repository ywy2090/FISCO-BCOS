/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Single TE gate for EIP-2929 address/storage warm tracking (Scheme A).
 * @file Eip2929Gate.h
 *
 * Chain policy may mask eip2929 while revision stays high (Scheme A deviation).
 * Do not read cfg.eip2929 outside this header in eth/ production code.
 *
 * Storage opcode gas constants live in `eip/Eip2929StorageGas.h` (gas layer).
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"

namespace bcos::evm::execution
{

/// TE runtime gate for EIP-2929 warm/cold tracking (Host + tx-entry + CREATE pin).
inline bool isEip2929Enabled(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return cfg.eip2929;
}

/// EIP-3651 coinbase warm at tx entry (independent of 2929 total gate).
inline bool isCoinbaseWarmEnabled(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return cfg.eip3651;
}

/// CREATE target warm pin (Frame / CreateDeployment).
inline bool isCreateWarmPinEnabled(bcos::evm::RevisionConfig const& cfg) noexcept
{
    return isEip2929Enabled(cfg);
}

}  // namespace bcos::evm::execution
