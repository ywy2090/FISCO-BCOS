#pragma once

#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <evmc/evmc.h>
#include <string_view>

namespace bcos::evm
{

inline constexpr evmc_address EMPTY_EVM_ADDRESS = {};
inline constexpr evmc_bytes32 EMPTY_EVM_BYTES32 = {};
inline constexpr evmc_uint256be EMPTY_EVM_UINT256 = {};

inline constexpr std::string_view USER_APPS_PREFIX = "/apps/";

/// FIB-75: balance transfer debited before EVM on FISCO path (same numeric value as TX_BASE_GAS).
inline constexpr int64_t BALANCE_TRANSFER_GAS = gas::TX_BASE_GAS;

/// FISCO chain precompile address range minimum (see PrecompiledManager lookup).
inline constexpr uint64_t FISCO_PRECOMPILE_MIN = 0x1000;

}  // namespace bcos::evm
