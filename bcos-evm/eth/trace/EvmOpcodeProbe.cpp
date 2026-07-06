/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file EvmOpcodeProbe.cpp
 */

#include "bcos-evm/eth/trace/EvmOpcodeProbe.h"
#include <algorithm>
#include <evmone/vm.hpp>
#include <iostream>
#include <memory>
#include <vector>

namespace bcos::evm::trace
{
namespace
{
struct TxOpcodeStats
{
    std::array<uint64_t, 256> count{};
    std::array<int64_t, 256> gas{};
    int64_t lastGas{-1};
    uint8_t pendingOpcode{0xFF};
    int64_t topLevelGasIn{0};
    int64_t topLevelGasOut{0};
    uint64_t frameCount{0};
};

thread_local TxOpcodeStats g_txOpcodeStats;

struct OpcodeRow
{
    uint8_t op{0};
    uint64_t count{0};
    int64_t gas{0};
};

char const* nameOrHex(uint8_t op) noexcept
{
    switch (op)
    {
    case 0x31:
        return "BALANCE";
    case 0x3b:
        return "EXTCODEHASH";
    case 0x3f:
        return "EXTCODESIZE";
    case 0x54:
        return "SLOAD";
    case 0x55:
        return "SSTORE";
    case 0x56:
        return "JUMP";
    case 0x57:
        return "JUMPI";
    case 0xf0:
        return "CREATE";
    case 0xf1:
        return "CALL";
    case 0xf2:
        return "CALLCODE";
    case 0xf4:
        return "DELEGATECALL";
    case 0xf5:
        return "CREATE2";
    case 0xff:
        return "SELFDESTRUCT";
    default:
        return nullptr;
    }
}
}  // namespace

void EvmOpcodeProbe::reset() noexcept
{
    g_txOpcodeStats = {};
}

void EvmOpcodeProbe::attachIfNeeded(evmc_vm* vm)
{
    if (vm == nullptr)
    {
        return;
    }
    auto& evmoneVm = *static_cast<evmone::VM*>(vm);
    if (evmoneVm.get_tracer() != nullptr)
    {
        return;
    }
    evmoneVm.add_tracer(std::make_unique<EvmOpcodeProbe>());
}

char const* EvmOpcodeProbe::opcodeName(uint8_t op) noexcept
{
    return nameOrHex(op);
}

void EvmOpcodeProbe::flushPendingGas(int64_t gasAfter) noexcept
{
    auto& s = g_txOpcodeStats;
    if (s.lastGas < 0 || s.pendingOpcode == 0xFF)
    {
        return;
    }
    auto const spent = s.lastGas - gasAfter;
    if (spent > 0)
    {
        s.gas[s.pendingOpcode] += spent;
    }
}

void EvmOpcodeProbe::on_execution_start(
    evmc_revision /*rev*/, const evmc_message& msg, evmc::bytes_view /*code*/) noexcept
{
    auto& s = g_txOpcodeStats;
    ++s.frameCount;
    if (msg.depth == 0)
    {
        s.topLevelGasIn = msg.gas;
    }
    s.lastGas = msg.gas;
    s.pendingOpcode = 0xFF;
    m_execDepth = msg.depth;
}

void EvmOpcodeProbe::on_instruction_start(uint32_t pc, const intx::uint256* /*stack_top*/,
    int /*stack_height*/, int64_t gas, const evmone::ExecutionState& state) noexcept
{
    flushPendingGas(gas);

    uint8_t op = 0;
    if (pc < state.original_code.size())
    {
        op = state.original_code[pc];
    }
    ++g_txOpcodeStats.count[op];
    g_txOpcodeStats.pendingOpcode = op;
    g_txOpcodeStats.lastGas = gas;
}

void EvmOpcodeProbe::on_execution_end(const evmc_result& result) noexcept
{
    flushPendingGas(result.gas_left);
    auto& s = g_txOpcodeStats;
    s.lastGas = -1;
    s.pendingOpcode = 0xFF;

    if (m_execDepth == 0)
    {
        s.topLevelGasOut = result.gas_left;
    }
}

void EvmOpcodeProbe::print(std::ostream& out)
{
    auto const& s = g_txOpcodeStats;
    int64_t const attributed = [&]() {
        int64_t sum = 0;
        for (auto g : s.gas)
        {
            sum += g;
        }
        return sum;
    }();

    out << "=== EEST_OPCODE_PROBE ===\n"
        << "frames=" << s.frameCount << " topGasIn=" << s.topLevelGasIn
        << " topGasOut=" << s.topLevelGasOut
        << " topGasSpent=" << (s.topLevelGasIn - s.topLevelGasOut)
        << " attributedOpcodeGas=" << attributed << "\n";

    std::vector<OpcodeRow> rows;
    rows.reserve(64);
    for (uint16_t op = 0; op < 256; ++op)
    {
        if (s.count[op] == 0)
        {
            continue;
        }
        rows.push_back(OpcodeRow{static_cast<uint8_t>(op), s.count[op], s.gas[op]});
    }
    std::sort(rows.begin(), rows.end(),
        [](OpcodeRow const& a, OpcodeRow const& b) { return a.gas > b.gas; });

    out << "opcode histogram (by gas):\n";
    for (auto const& row : rows)
    {
        if (auto const* name = opcodeName(row.op); name != nullptr)
        {
            out << "  " << name << "(0x";
            if (row.op < 16)
            {
                out << '0';
            }
            out << std::hex << static_cast<unsigned>(row.op) << std::dec << "): count=" << row.count
                << " gas=" << row.gas << "\n";
        }
    }
    out << "all opcodes:\n";
    for (auto const& row : rows)
    {
        out << "  0x";
        if (row.op < 16)
        {
            out << '0';
        }
        out << std::hex << static_cast<unsigned>(row.op) << std::dec << ": count=" << row.count
            << " gas=" << row.gas << "\n";
    }
}

}  // namespace bcos::evm::trace
