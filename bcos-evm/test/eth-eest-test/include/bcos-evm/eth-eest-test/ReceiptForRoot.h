#pragma once

#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-protocol/TransactionStatus.h>
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

/// geth receiptsRoot status bit: SUCCESS → 0x01; failure → 0x80 (RLP empty).
/// Settlement may normalize status_code to SUCCESS (ADR-015 / EIP-7702) while receiptStatus
/// preserves failure for MPT encoding (Gap 40).
inline evmc_status_code receiptMptStatus(evmc_status_code settlementStatus,
    bcos::protocol::TransactionStatus receiptStatus, bool topLevelIncludedTxVmError) noexcept
{
    if (topLevelIncludedTxVmError)
        return EVMC_REVERT;
    if (receiptStatus != bcos::protocol::TransactionStatus::None)
        return EVMC_REVERT;
    return settlementStatus;
}

}  // namespace bcos::evm::reference_tests
