/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Single TE gate for EIP-2929 address/storage warm tracking (Scheme A).
 * @file Eip2929Access.h
 *
 * Chain policy may mask warm_access while revision stays high (Scheme A deviation).
 * Do not read cfg.warm_access outside this header in eth/ production code.
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"

namespace bcos::evm::execution
{

/// TE runtime gate for EIP-2929 warm/cold tracking (Host + tx-entry + CREATE pin).
inline bool isEip2929Enabled(bcos::evm_standard::RevisionConfig const& cfg) noexcept
{
    return cfg.warm_access;
}

/// EIP-3651 coinbase warm at tx entry (independent of 2929 total gate).
inline bool isCoinbaseWarmEnabled(bcos::evm_standard::RevisionConfig const& cfg) noexcept
{
    return cfg.eip3651;
}

/// CREATE target warm pin (Frame / CreateContract).
inline bool isCreateWarmPinEnabled(bcos::evm_standard::RevisionConfig const& cfg) noexcept
{
    return isEip2929Enabled(cfg);
}

}  // namespace bcos::evm::execution
