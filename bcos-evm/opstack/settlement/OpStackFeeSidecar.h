#pragma once

// Per-transaction mutable fee state shared across buyGas, EVM execution, and post-settlement.
// Written by OpStackFeeSettlement::buyGas; read by finalizeNormal and receipt projection.

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

struct OpStackFeeSidecar
{
    bcos::u256 effectiveGasPrice{0};  // EIP-1559 effective price after buyGas
    bcos::u256 baseFee{0};            // block base fee at debit time
    bcos::u256 l1CostCharged{0};      // L1 data fee pre-debited from sender
    bcos::u256 operatorCostLimit{0};  // operator fee ceiling pre-debited
    uint64_t floorDataGas{0};         // Regolith+ minimum data gas for postExecuteGasSettlement
};

}  // namespace bcos::evm
