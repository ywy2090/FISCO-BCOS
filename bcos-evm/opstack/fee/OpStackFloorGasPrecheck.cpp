#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-protocol/TransactionStatus.h"
#include "eth/kernel/EVMCResult.h"
#include <evmc/evmc.h>

namespace bcos::evm
{
std::optional<EVMCResult> opStackFloorGasPrecheck(OpStackFloorGasPrecheckInput const& input)
{
    // Value transfer affordability (distinct from gas fee balance checked at buyGas).
    auto const value = state::fromEvmC(input.message.value);
    if (!input.skipTransactionChecks && value != 0 &&
        input.state.get_balance(input.message.sender) < value)
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        return EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
    }

    auto const floorCheck = executeEntryFloorDataGasCheck(input.gasLimit, input.inputData);
    input.floorDataGasOut = floorCheck.floorGas;
    // Reject when gasLimit < floorDataGas (EIP-7623 execute-entry rule).
    if (floorCheck.ok)
    {
        return std::nullopt;
    }

    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
}
}  // namespace bcos::evm
