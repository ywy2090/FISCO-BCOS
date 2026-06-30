/*
 * @brief Isthmus EVM revision binding (Prague at tx granularity).
 * @file OpStackIsthmusRevision.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"

namespace bcos::evm
{

/// Isthmus binds to Prague EVM revision at tx granularity. Block-level Prague
/// postExecution (EIP-6110/7002/7251) is out of scope — op-geth skips it when
/// IsIsthmus (state_processor.go:141). Guarded by IsthmusPostExecutionPolicyTest
/// and check-opstack-no-prague-post-execution.sh.
inline bcos::evm_standard::RevisionConfig makeIsthmusRevisionConfig()
{
    return bcos::evm_standard::revisionConfigFromRevision(EVMC_PRAGUE);
}

}  // namespace bcos::evm
