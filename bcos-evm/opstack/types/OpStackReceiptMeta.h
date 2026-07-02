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
    std::optional<uint64_t> daFootprint;  // op-geth Receipt.BlobGasUsed 的 Jovian 语义
};
}  // namespace bcos::evm
