/*
 * @brief Isthmus EVM revision binding (Prague at tx granularity).
 * @file OpStackIsthmusRevision.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"

namespace bcos::evm
{

/// Isthmus binds to Prague EVM revision at tx granularity. Block-level Prague
/// postExecution (EIP-6110/7002/7251) is out of scope — skipped at Isthmus.
/// Guarded by IsthmusPostExecutionPolicyTest and check-opstack-no-prague-post-execution.sh.
inline bcos::evm::RevisionConfig makeIsthmusRevisionConfig()
{
    return bcos::evm::revisionConfigFromRevision(EVMC_PRAGUE);
}

}  // namespace bcos::evm
