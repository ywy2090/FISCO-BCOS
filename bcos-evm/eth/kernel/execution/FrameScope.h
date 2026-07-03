/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief TopLevel vs Nested execution frame scope (kernel shared type).
 * @file execution/FrameScope.h
 */

#pragma once

namespace bcos::evm::execution
{
enum class FrameScope
{
    TopLevel,
    Nested,
};
}  // namespace bcos::evm::execution
