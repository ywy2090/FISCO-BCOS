#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

/// Lifecycle-mutable OpStack fee state (ADR-021 Appendix A).
struct OpStackFeeSidecar
{
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
    uint64_t floorDataGas{0};
};

}  // namespace bcos::evm
