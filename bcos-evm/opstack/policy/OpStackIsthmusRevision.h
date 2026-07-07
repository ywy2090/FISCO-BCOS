/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OP Stack EVM revision → RevisionConfig binding.
 * @file OpStackIsthmusRevision.h
 *
 * Maps an evmc_revision (or block header + ledger features) to the canonical RevisionConfig
 * used by OP Stack execution. Block-level Prague postExecution (EIP-6110/7002/7251) is out of
 * scope — OP Stack runs at tx granularity only. Guarded by IsthmusPostExecutionPolicyTest and
 * check-opstack-no-prague-post-execution.sh.
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Protocol.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

/// Resolve EVM revision from ledger features and block header version (mirrors Fisco
/// toFiscoRevision).
inline evmc_revision evmcRevisionFromOpStackBlock(
    ledger::Features const& features, uint32_t blockVersion)
{
    using Flag = ledger::Features::Flag;
    if (features.get(Flag::feature_evm_osaka))
    {
        return EVMC_OSAKA;
    }
    if (features.get(Flag::feature_evm_prague))
    {
        return EVMC_PRAGUE;
    }
    if (blockVersion >= static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION))
    {
        return features.get(Flag::feature_evm_cancun) ? EVMC_CANCUN : EVMC_PARIS;
    }
    return EVMC_LONDON;
}

/// Canonical RevisionConfig for the given EVM revision (delegates to revisionConfigFromRevision).
inline RevisionConfig makeOpStackRevisionConfig(evmc_revision revision)
{
    return revisionConfigFromRevision(revision);
}

/// RevisionConfig derived from block header version and chain features.
inline RevisionConfig makeOpStackRevisionConfigFromBlock(
    protocol::BlockHeader const& header, ledger::Features const& features)
{
    return makeOpStackRevisionConfig(evmcRevisionFromOpStackBlock(features, header.version()));
}

/// Isthmus default: Prague at tx granularity (production baseline when block context unavailable).
inline RevisionConfig makeIsthmusRevisionConfig()
{
    return makeOpStackRevisionConfig(EVMC_PRAGUE);
}

}  // namespace bcos::evm
