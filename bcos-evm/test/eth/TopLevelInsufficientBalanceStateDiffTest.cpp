/*
 * GAP-TE-003 characterization: EIP-7702 SetCode auth may commit before top-level
 * INSUFFICIENT_BALANCE; normalizeIncludedTxVmerr skips that status; TE skips applyStateDiff.
 *
 * GETH_ORACLE: go-ethereum/core/state_processor_test.go:165-170 (ErrInsufficientFundsForTransfer)
 * rejects tx — no partial delegation state on chain.
 */
#define BOOST_TEST_MODULE TopLevelInsufficientBalanceStateDiffTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/NormalizeIncludedTxVmerr.h"
#include "bcos-evm/eth/apply/EthReferenceExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include "helpers/SetCodeAuthorizationTestHelper.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_uint256be weiValue(uint64_t amount)
{
    evmc_uint256be out{};
    out.bytes[31] = static_cast<uint8_t>(amount & 0xff);
    if (amount > 0xff)
    {
        out.bytes[30] = static_cast<uint8_t>((amount >> 8) & 0xff);
    }
    return out;
}

bcos::evm_standard::RevisionConfig prague7702Config()
{
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_PRAGUE;
    cfg.eip7702 = true;
    cfg.eip7623 = true;
    cfg.warm_access = true;
    cfg.calldata_floor_per_token = 10;
    return cfg;
}

// Mirrors EthTransactionExecutorImpl Execute phase applyStateDiff gate.
bool teWouldApplyStateDiff(evmc_status_code status) noexcept
{
    return status == EVMC_SUCCESS || status == EVMC_REVERT;
}

bool has7702DelegationCode(state::StateDiff const& diff, evmc_address const& account)
{
    auto const it = diff.accounts.find(account);
    if (it == diff.accounts.end())
    {
        return false;
    }
    auto const& code = it->second.code;
    return code.size() == 23 && code[0] == 0xEF && code[1] == 0x01 && code[2] == 0x00;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TopLevelInsufficientBalanceStateDiffTest)

// GAP-TE-003: kernel applies 7702 auth, then top-level value transfer fails; stateDiff
// retains delegation but TE gate skips applyStateDiff for INSUFFICIENT_BALANCE.
BOOST_AUTO_TEST_CASE(setcode_auth_applied_then_top_level_balance_failure_state_diff)
{
    auto const authKey = TestAuthKeyPair::generate();
    auto const sender = authKey.address();
    auto const recipient = addressFromLastByte(0x32);
    auto const delegationTarget = addressFromLastByte(0x42);

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 50, .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = weiValue(100);

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    state::State state(stateView);
    ExecuteMessageInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = prague7702Config();
    input.authorizationListPresent = true;
    input.authorizations.push_back(authKey.sign(delegationTarget, 1));

    auto output = innerExecute(std::move(input));

    // CURRENT_ORACLE: execution returns INSUFFICIENT_BALANCE after auth path ran.
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK(has7702DelegationCode(output.stateDiff, sender));

    auto raw = output.result.release_raw();
    EVMCResult normalized(
        raw, bcos::protocol::TransactionStatus::NotEnoughCash);
    normalizeIncludedTxVmerr(normalized, /*depth=*/0);

    // normalizeIncludedTxVmerr does NOT map INSUFFICIENT_BALANCE to SUCCESS.
    BOOST_CHECK_EQUAL(normalized.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(static_cast<int>(normalized.status),
        static_cast<int>(bcos::protocol::TransactionStatus::NotEnoughCash));

    // CURRENT_ORACLE: TE skips applyStateDiff → partial auth diff is dropped at storage layer.
    BOOST_CHECK(!teWouldApplyStateDiff(normalized.status_code));
    BOOST_CHECK(!output.stateDiff.accounts.empty());

#if 0  // GETH_ORACLE: tx rejected — delegation must not persist (state_processor_test.go:165-170)
    BOOST_CHECK(output.stateDiff.accounts.empty());
    BOOST_CHECK(teWouldApplyStateDiff(normalized.status_code));
#endif
}

// Contrast: ethReferenceExecute precheck exits before auth when value exceeds balance.
// GETH_ORACLE: same reject — go-ethereum/core/state_processor_test.go:165-170.
BOOST_AUTO_TEST_CASE(eth_reference_precheck_rejects_before_auth_when_value_exceeds_balance)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    auto const authKey = TestAuthKeyPair::generate();
    auto const sender = authKey.address();
    auto const recipient = addressFromLastByte(0x55);
    auto const delegationTarget = addressFromLastByte(0x66);

    state::test::InMemoryStateView view;
    view.insert_account(sender, state::Account{.balance = 50, .nonce = 0});
    view.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = weiValue(100);

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    EthReferenceRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = prague7702Config();
    input.authorizationListPresent = true;
    input.authorizations.push_back(authKey.sign(delegationTarget, 1));
    input.web3TypedTxKind = 4;

    auto output = task::syncWait(ethReferenceExecute(std::move(input)));

    // CURRENT_ORACLE: precheck maps to InsufficientFunds (10015), auth never applied.
    BOOST_CHECK(!output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(static_cast<int>(output.evmcResult.status),
        static_cast<int>(bcos::protocol::TransactionStatus::InsufficientFunds));
    BOOST_CHECK(!has7702DelegationCode(output.stateDiff, sender));
    BOOST_CHECK(!teWouldApplyStateDiff(output.evmcResult.status_code));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
