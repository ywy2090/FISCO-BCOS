#pragma once

#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <vector>

namespace bcos::evm::reference_tests
{

/// Minimal receipt fields for MPT receiptsRoot (avoids BlockTransition.h include cycle).
struct ReceiptForRoot
{
    uint8_t txType = 0;  // EIP-2718 typed receipt prefix (0 = legacy)
    evmc_status_code status = EVMC_SUCCESS;
    uint64_t cumulativeGasUsed = 0;
    bcos::bytes bloom;  // 256 bytes
    std::vector<state::LogEntry> logs;
};

}  // namespace bcos::evm::reference_tests
