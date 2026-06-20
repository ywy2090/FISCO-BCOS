/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared SELFDESTRUCT compat bytecode for SD-B / SD-C matrix tests.
 *  @file SelfdestructCompatBytecode.h
 */
#pragma once

#include <string_view>

namespace bcos::evm::test::selfdestruct_compat
{
constexpr std::string_view kSelfdestructTargetHex = "0000000000000000000000000000000000000012";
constexpr std::string_view kBeneficiaryHex = "00000000000000000000000000000000000000bb";
constexpr std::string_view kSelfdestructTail = "73000000000000000000000000000000000000bbff";
constexpr std::string_view kSelfdestructRuntimeCode = kSelfdestructTail;
constexpr std::string_view kSelfdestructInitCode = kSelfdestructTail;
}  // namespace bcos::evm::test::selfdestruct_compat
