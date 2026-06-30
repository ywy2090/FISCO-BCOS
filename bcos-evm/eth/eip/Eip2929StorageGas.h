/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief EIP-2929 / EIP-2200 / EIP-3529 storage gas constants (geth operations_acl.go).
 * @file Eip2929StorageGas.h
 */
#pragma once

#include <cstdint>

namespace bcos::evm::gas
{

inline constexpr uint64_t SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529 = 4'800;
inline constexpr uint64_t SSTORE_SET_GAS_EIP2200 = 20'000;
inline constexpr uint64_t SSTORE_RESET_GAS_EIP2200 = 5'000;
inline constexpr uint64_t COLD_SLOAD_COST_EIP2929 = 2'100;
inline constexpr uint64_t WARM_STORAGE_READ_COST_EIP2929 = 100;

}  // namespace bcos::evm::gas
