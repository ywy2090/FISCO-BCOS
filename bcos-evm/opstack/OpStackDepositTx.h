#pragma once

#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{
struct OpStackDepositTx
{
    bcos::h256 sourceHash{};
    evmc_address from{};
    std::optional<evmc_address> to;
    std::optional<bcos::u256> mint;
    bcos::u256 value{0};
    uint64_t gas{0};
    bool isSystemTransaction{false};
    bcos::bytes data;
};
}  // namespace bcos::evm
