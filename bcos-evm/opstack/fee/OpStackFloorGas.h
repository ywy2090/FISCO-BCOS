#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>
#include <optional>

namespace bcos::evm
{

constexpr uint64_t OP_TX_GAS = 21'000;
constexpr uint64_t OP_TX_TOKEN_PER_NON_ZERO_BYTE = 4;
constexpr uint64_t OP_TX_COST_FLOOR_PER_TOKEN = 10;

enum class FloorDataGasError
{
    GasUintOverflow = 1,
};

enum class ExecuteEntryFloorError
{
    BelowFloor = 1,
    GasUintOverflow = 2,
};

struct FloorDataGasResult
{
    uint64_t gas{0};
    std::optional<FloorDataGasError> error{};

    explicit operator bool() const noexcept { return !error.has_value(); }
};

struct ExecuteEntryFloorCheck
{
    bool ok{false};
    ExecuteEntryFloorError error{};
    uint64_t floorGas{0};
    uint64_t gasLimit{0};
};

// Mirrors op-geth FloorDataGas (state_transition.go:120-133).
FloorDataGasResult tryFloorDataGas(
    bcos::bytesConstRef data, std::optional<uint64_t> tokenOverride = std::nullopt);

uint64_t floorDataGas(bcos::bytesConstRef data);

// Stub for Task 8 wiring: execute-entry gasLimit >= floorDataGas (§7.1.1 step 4).
ExecuteEntryFloorCheck executeEntryFloorDataGasCheck(uint64_t gasLimit, bcos::bytesConstRef data);

}  // namespace bcos::evm
