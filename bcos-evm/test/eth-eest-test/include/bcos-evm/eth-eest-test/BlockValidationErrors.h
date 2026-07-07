#pragma once

namespace bcos::evm::reference_tests::BlockError
{
inline constexpr auto INVALID_BLOCK_PARENT = "INVALID_BLOCK_PARENT";
inline constexpr auto INVALID_BLOCK_NUMBER = "INVALID_BLOCK_NUMBER";
inline constexpr auto INCORRECT_BLOCK_FORMAT = "INCORRECT_BLOCK_FORMAT";
inline constexpr auto INVALID_GASLIMIT = "INVALID_GASLIMIT";
inline constexpr auto INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT =
    "INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT";
inline constexpr auto INVALID_BASEFEE_PER_GAS = "INVALID_BASEFEE_PER_GAS";
inline constexpr auto INCORRECT_EXCESS_BLOB_GAS = "INCORRECT_EXCESS_BLOB_GAS";
inline constexpr auto INCORRECT_BLOB_GAS_USED = "INCORRECT_BLOB_GAS_USED";
inline constexpr auto RLP_BLOCK_LIMIT_EXCEEDED = "RLP_BLOCK_LIMIT_EXCEEDED";
}  // namespace bcos::evm::reference_tests::BlockError
