/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Ethereum mainnet block → RevisionConfig binding.
 * @file EthMainnetRevision.h
 *
 * Composes EthForkSchedule (block → evmc_revision) with revisionConfigFromRevision
 * (eth/core/RevisionConfig.h). Eth reference path uses maximal mainnet gates with no
 * ledger feature mask (contrast FiscoPolicy / OpStackIsthmusRevision).
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/policy/EthForkSchedule.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

inline RevisionConfig makeEthRevisionConfig(evmc_revision revision)
{
    return revisionConfigFromRevision(revision);
}

inline RevisionConfig makeEthRevisionConfigFromBlock(protocol::BlockHeader const& header)
{
    return makeEthRevisionConfig(evmcRevisionFromBlockNumber(header.number()));
}

}  // namespace bcos::evm
