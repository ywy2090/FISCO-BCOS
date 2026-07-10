#pragma once

#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <test/state/transaction.hpp>

namespace bcos::evmref::opstack
{
struct OpFeeParams;
struct OpForkConfig;

struct OpReceiptMeta
{
    // L1 直通（op-geth: L1GasPrice / L1BlobBaseFee / L1BaseFeeScalar / L1BlobBaseFeeScalar /
    // L1Fee）
    std::optional<intx::uint256> l1_gas_price;      // = fee.l1_base_fee
    std::optional<intx::uint256> l1_blob_base_fee;  // = fee.blob_base_fee
    std::optional<uint32_t> l1_base_fee_scalar;
    std::optional<uint32_t> l1_blob_base_fee_scalar;
    std::optional<intx::uint256> l1_fee;  // = l1_cost
    // operator（Isthmus+）
    std::optional<intx::uint256> operator_fee;  // FISCO 扩展：实收值（op-geth receipt 无此字段）
    std::optional<uint32_t> operator_fee_scalar;  // 仅 (scalar≠0 ∨ constant≠0) 时填
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint（Jovian+；op-geth receipt BlobGasUsed 语义）
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
};

struct OpTxReceipt
{
    evmone::state::TransactionReceipt receipt;
    OpReceiptMeta meta;
};

OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee,
    evmc::bytes_view signedTxEnvelope, intx::uint256 l1_cost, intx::uint256 operator_fee_at_used,
    bool fill_operator_scalars) noexcept;
}  // namespace bcos::evmref::opstack
