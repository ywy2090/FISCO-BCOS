/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Op Stack receipt extension fields (beyond standard Ethereum receipt).
 * @file OpStackReceiptMeta.h
 *
 * Populated by projectOpStackReceiptMeta (opstack/fee/OpStackReceiptMetaProjection.h).
 *
 * Field semantics:
 *   l1Fee                — L1 data fee routed to L1 fee vault (= l1CostCharged from buyGas)
 *   operatorFee          — Actual operator fee at gasUsed (Isthmus+)
 *   operatorFeeScalar    — L1Block slot-8 scalar echoed on receipt when non-zero
 *   operatorFeeConstant  — L1Block slot-8 constant echoed on receipt when non-zero
 *   depositNonce         — L1-origin deposit account nonce (deposit txs only)
 *   depositReceiptVersion— Canyon+ deposit receipt version marker
 *   daFootprintGasScalar — Jovian scalar from L1Block slot 8
 *   daFootprint          — estimatedDASize * daFootprintGasScalar (Jovian; op-geth BlobGasUsed)
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <optional>

namespace bcos::evm
{
struct OpStackReceiptMeta
{
    std::optional<bcos::u256> l1Fee;
    std::optional<bcos::u256> operatorFee;
    std::optional<bcos::u256> operatorFeeScalar;
    std::optional<bcos::u256> operatorFeeConstant;
    std::optional<uint64_t> depositNonce;
    std::optional<uint64_t> depositReceiptVersion;
    std::optional<uint64_t> daFootprintGasScalar;
    std::optional<uint64_t> daFootprint;
};
}  // namespace bcos::evm
