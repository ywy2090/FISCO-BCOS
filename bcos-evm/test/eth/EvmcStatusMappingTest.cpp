/*
 * GAP-005 / GAP-009 / GAP-010 / GAP-011: dual EVMC status mapping tables vs adoptEvmcResult.
 *
 * GETH_ORACLE: go-ethereum/core/vm/errors.go:30-205 (vm.Error ↔ RPC ErrorCode); no EVMC layer.
 * STATIC_MODE_VIOLATION ↔ ErrWriteProtection (errors.go:36); unknown → VMErrorCodeUnknown (205).
 */
#define BOOST_TEST_MODULE EvmcStatusMappingTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state-transition/AdoptEvmcResult.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/test/included/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::test
{
namespace
{
using bcos::protocol::TransactionStatus;

struct MappingExpectation
{
    evmc_status_code evmcStatus;
    char const* label;
    std::optional<TransactionStatus> transactionStatus;
    bool transactionStatusThrows{false};
    TransactionStatus errorMessageStatus;
    bool errorMessageHasOutput{false};
    char const* gethOracleNote{nullptr};
};

std::vector<MappingExpectation> mappingTable()
{
    return {
        {EVMC_SUCCESS, "SUCCESS", TransactionStatus::None, false, TransactionStatus::None, false,
            "errors.go — nil error"},
        {EVMC_REVERT, "REVERT", TransactionStatus::RevertInstruction, false,
            TransactionStatus::RevertInstruction, false, "errors.go:32 ErrExecutionReverted"},
        {EVMC_OUT_OF_GAS, "OUT_OF_GAS", TransactionStatus::OutOfGas, false,
            TransactionStatus::OutOfGas, true, "errors.go:28 ErrOutOfGas"},
        {EVMC_INSUFFICIENT_BALANCE, "INSUFFICIENT_BALANCE", TransactionStatus::NotEnoughCash, false,
            TransactionStatus::NotEnoughCash, false,
            "errors.go:30 ErrInsufficientBalance → VMErrorCodeInsufficientBalance:143"},
        {EVMC_STACK_OVERFLOW, "STACK_OVERFLOW", TransactionStatus::OutOfStack, false,
            TransactionStatus::OutOfStack, true, "errors.go:62+ ErrStackOverflow"},
        {EVMC_STACK_UNDERFLOW, "STACK_UNDERFLOW", TransactionStatus::StackUnderflow, false,
            TransactionStatus::StackUnderflow, true, "errors.go:47+ ErrStackUnderflow"},
        {EVMC_INVALID_INSTRUCTION, "INVALID_INSTRUCTION", TransactionStatus::BadInstruction, false,
            TransactionStatus::BadInstruction, true, "errors.go:77+ ErrInvalidOpCode"},
        {EVMC_UNDEFINED_INSTRUCTION, "UNDEFINED_INSTRUCTION", TransactionStatus::BadInstruction,
            false, TransactionStatus::BadInstruction, true, "errors.go:77+ ErrInvalidOpCode"},
        {EVMC_BAD_JUMP_DESTINATION, "BAD_JUMP_DESTINATION", std::nullopt, true,
            TransactionStatus::BadJumpDestination, true, "errors.go:35 ErrInvalidJump"},
        {EVMC_INVALID_MEMORY_ACCESS, "INVALID_MEMORY_ACCESS", std::nullopt, true,
            TransactionStatus::StackUnderflow, true,
            "errors.go:47+ ErrStackUnderflow (no memory OOB vm.Error)"},
        {EVMC_STATIC_MODE_VIOLATION, "STATIC_MODE_VIOLATION", std::nullopt, true,
            TransactionStatus::Unknown, true, "errors.go:36 ErrWriteProtection"},
        {EVMC_INTERNAL_ERROR, "INTERNAL_ERROR", std::nullopt, true, TransactionStatus::Unknown,
            false, "errors.go:205 VMErrorCodeUnknown fallback"},
        {static_cast<evmc_status_code>(999), "UNKNOWN_999", std::nullopt, true,
            TransactionStatus::Unknown, false, "errors.go:205 VMErrorCodeUnknown"},
    };
}

evmc_result rawStatus(evmc_status_code status, int64_t gasLeft = 0)
{
    evmc_result raw{};
    raw.status_code = status;
    raw.gas_left = gasLeft;
    return raw;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EvmcStatusMappingTest)

BOOST_AUTO_TEST_CASE(evmc_status_mapping_completeness_table)
{
    bcos::crypto::Keccak256 hashImpl;

    for (auto const& row : mappingTable())
    {
        BOOST_TEST_CONTEXT(row.label)
        {
            if (row.transactionStatusThrows)
            {
                BOOST_CHECK_THROW(evmcStatusToTransactionStatus(row.evmcStatus), bcos::Exception);
            }
            else
            {
                BOOST_REQUIRE(row.transactionStatus.has_value());
                BOOST_CHECK_EQUAL(
                    evmcStatusToTransactionStatus(row.evmcStatus), *row.transactionStatus);
            }

            auto const [msgStatus, msgOutput] = evmcStatusToErrorMessage(hashImpl, row.evmcStatus);
            BOOST_CHECK_EQUAL(msgStatus, row.errorMessageStatus);
            BOOST_CHECK_EQUAL(!msgOutput.empty(), row.errorMessageHasOutput);

            auto adopted = adoptEvmcResult(evmc::Result(rawStatus(row.evmcStatus)), hashImpl);
            BOOST_CHECK_EQUAL(adopted.status_code, row.evmcStatus);
            BOOST_CHECK_EQUAL(adopted.status, row.errorMessageStatus);

            if (row.transactionStatusThrows)
            {
                // GAP-010: EVMCResult single-arg ctor (VMInstance path) throws while adopt/adopt
                // path uses the wider error-message table.
                BOOST_CHECK_THROW(EVMCResult{rawStatus(row.evmcStatus)}, bcos::Exception);
            }
            else
            {
                EVMCResult constructed{rawStatus(row.evmcStatus)};
                BOOST_CHECK_EQUAL(constructed.status, *row.transactionStatus);
                BOOST_CHECK_EQUAL(constructed.status, adopted.status);
            }

            (void)row.gethOracleNote;
        }
    }
}

// GAP-009: precheck InsufficientFunds(10015) vs execution NotEnoughCash(7) for same EVMC code.
BOOST_AUTO_TEST_CASE(insufficient_balance_precheck_vs_execution_status_codes)
{
    evmc_result raw{};
    raw.status_code = EVMC_INSUFFICIENT_BALANCE;

    EVMCResult executionPath(raw, TransactionStatus::NotEnoughCash);
    EVMCResult precheckPath(raw, TransactionStatus::InsufficientFunds);

    BOOST_CHECK_EQUAL(executionPath.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(precheckPath.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(static_cast<int>(executionPath.status), 7);
    BOOST_CHECK_EQUAL(static_cast<int>(precheckPath.status), 10015);
    // GETH_ORACLE: block reject before execution (state_processor_test.go:165-170) — no split.
}

// GAP-010 / §2-映射 focus cases (also covered in table; explicit anchors for review pack).
BOOST_AUTO_TEST_CASE(invalid_memory_access_maps_to_stack_underflow_in_error_table_only)
{
    bcos::crypto::Keccak256 hashImpl;
    auto const [status, output] = evmcStatusToErrorMessage(hashImpl, EVMC_INVALID_MEMORY_ACCESS);
    BOOST_CHECK_EQUAL(status, TransactionStatus::StackUnderflow);
    BOOST_CHECK(!output.empty());
    BOOST_CHECK_THROW(evmcStatusToTransactionStatus(EVMC_INVALID_MEMORY_ACCESS), bcos::Exception);
}

BOOST_AUTO_TEST_CASE(static_mode_violation_maps_to_unknown_in_error_table_only)
{
    bcos::crypto::Keccak256 hashImpl;
    auto const [status, output] = evmcStatusToErrorMessage(hashImpl, EVMC_STATIC_MODE_VIOLATION);
    BOOST_CHECK_EQUAL(status, TransactionStatus::Unknown);
    BOOST_CHECK(!output.empty());
    BOOST_CHECK_THROW(evmcStatusToTransactionStatus(EVMC_STATIC_MODE_VIOLATION), bcos::Exception);
    // GETH_ORACLE: errors.go:36 ErrWriteProtection; runtime_test.go:617+ write protection cases.
}

// GAP-011: bcos-evm VMInstance::execute constructs EVMCResult(single-arg) — throws on unmapped
// statuses; adoptEvmcResult (pipeline path) uses the wider error-message table instead.
BOOST_AUTO_TEST_CASE(vm_instance_single_arg_constructor_throws_while_adopt_does_not)
{
    bcos::crypto::Keccak256 hashImpl;

    evmc_result raw{};
    raw.status_code = EVMC_STATIC_MODE_VIOLATION;
    raw.gas_left = 42'000;

    // CURRENT_ORACLE: VMInstance.cpp:22-23 EVMCResult(evmone::baseline::execute(...))
    BOOST_CHECK_THROW(EVMCResult{raw}, bcos::Exception);

    auto adopted = adoptEvmcResult(evmc::Result(raw), hashImpl);
    BOOST_CHECK_EQUAL(adopted.status_code, EVMC_STATIC_MODE_VIOLATION);
    BOOST_CHECK_EQUAL(adopted.status, TransactionStatus::Unknown);
    // adoptEvmcResult maps status only; output buffer may be empty for this status.
    auto const [mappedStatus, errorOutput] =
        evmcStatusToErrorMessage(hashImpl, EVMC_STATIC_MODE_VIOLATION);
    BOOST_CHECK_EQUAL(mappedStatus, TransactionStatus::Unknown);
    BOOST_CHECK(!errorOutput.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
