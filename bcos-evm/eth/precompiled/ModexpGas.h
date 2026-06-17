/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas and EIP-7823 validation.
 *  @file ModexpGas.h
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <cstddef>
#include <string_view>

namespace bcos::evm_standard
{
struct RevisionConfig;
}  // namespace bcos::evm_standard

namespace bcos::evm
{

struct ModexpLengths
{
    bool overflow = false;
    size_t baseLen = 0;
    size_t expLen = 0;
    size_t modLen = 0;
};

constexpr size_t MODEXP_MAX_FIELD_LEN_EIP7823 = 1024;

ModexpLengths parseModexpLengths(bcos::bytesConstRef input);

inline bool modexpEip7823Enabled(const bcos::evm_standard::RevisionConfig& rev) noexcept
{
    return rev.eip7823;
}

bool validateModexpEip7823(bcos::bytesConstRef input, evmc_revision revision);

bool shouldRejectModexpEip7823(evmc_address const& addr, bcos::bytesConstRef input,
    const bcos::evm_standard::RevisionConfig& rev, evmc_revision revision) noexcept;

bool shouldRejectModexpEip7823(std::string_view addr, bcos::bytesConstRef input,
    const bcos::evm_standard::RevisionConfig& rev, evmc_revision revision) noexcept;

/// EIP-198 (< Berlin), EIP-2565 (Berlin..Osaka-1), EIP-7883 (Osaka+).
bcos::bigint calcModexpGas(bcos::bytesConstRef input, evmc_revision revision);

/// Legacy EIP-198 pricing for PrecompiledRegistrar::pricer("modexp") and unit tests only.
/// Production uses PrecompiledContract::modexp() -> calcModexpGas(input, revision).
bcos::bigint calcModexpGasEip198Public(bcos::bytesConstRef input);

}  // namespace bcos::evm
