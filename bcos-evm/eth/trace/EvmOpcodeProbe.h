/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Opcode-level gas probe via evmone::Tracer (EEST_OPCODE_TRACE=1).
 * @file EvmOpcodeProbe.h
 */

#pragma once

#include <evmc/evmc.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <evmone/tracing.hpp>
#include <evmone/vm.hpp>
#include <iosfwd>

namespace bcos::evm::trace
{

/// Tx-scoped opcode histogram + attributed gas (debug only; remove after 6780 gas fix).
class EvmOpcodeProbe final : public evmone::Tracer
{
public:
    static bool enabled() noexcept
    {
        static int cached = -1;
        if (cached < 0)
        {
            cached = std::getenv("EEST_OPCODE_TRACE") != nullptr ? 1 : 0;
        }
        return cached != 0;
    }

    static void reset() noexcept;
    static void print(std::ostream& out);

    static void attachIfNeeded(evmc_vm* vm);

private:
    void on_execution_start(
        evmc_revision rev, const evmc_message& msg, evmc::bytes_view code) noexcept override;
    void on_instruction_start(uint32_t pc, const intx::uint256* stack_top, int stack_height,
        int64_t gas, const evmone::ExecutionState& state) noexcept override;
    void on_execution_end(const evmc_result& result) noexcept override;

    static char const* opcodeName(uint8_t op) noexcept;

    void flushPendingGas(int64_t gasAfter) noexcept;

    int m_execDepth{0};
};

}  // namespace bcos::evm::trace
