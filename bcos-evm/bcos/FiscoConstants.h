#pragma once

#include <evmc/evmc.h>
#include <string_view>

namespace bcos::evm
{

inline constexpr evmc_address EMPTY_EVM_ADDRESS = {};
inline constexpr evmc_bytes32 EMPTY_EVM_BYTES32 = {};
inline constexpr evmc_uint256be EMPTY_EVM_UINT256 = {};

inline constexpr std::string_view USER_APPS_PREFIX = "/apps/";

}  // namespace bcos::evm
