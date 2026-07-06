#define BOOST_TEST_MODULE OpStack7702RevertIncludedTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include "helpers/SetCodeAuthorizationTestHelper.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 packFeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar)
{
    constexpr size_t scalarSectionStart = 32 - 12 - 4;
    evmc_bytes32 out{};
    out.bytes[scalarSectionStart] = static_cast<uint8_t>((baseFeeScalar >> 24) & 0xff);
    out.bytes[scalarSectionStart + 1] = static_cast<uint8_t>((baseFeeScalar >> 16) & 0xff);
    out.bytes[scalarSectionStart + 2] = static_cast<uint8_t>((baseFeeScalar >> 8) & 0xff);
    out.bytes[scalarSectionStart + 3] = static_cast<uint8_t>(baseFeeScalar & 0xff);
    out.bytes[scalarSectionStart + 4] = static_cast<uint8_t>((blobBaseFeeScalar >> 24) & 0xff);
    out.bytes[scalarSectionStart + 5] = static_cast<uint8_t>((blobBaseFeeScalar >> 16) & 0xff);
    out.bytes[scalarSectionStart + 6] = static_cast<uint8_t>((blobBaseFeeScalar >> 8) & 0xff);
    out.bytes[scalarSectionStart + 7] = static_cast<uint8_t>(blobBaseFeeScalar & 0xff);
    return out;
}

evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant)
{
    evmc_bytes32 out{};
    out.bytes[20] = static_cast<uint8_t>((operatorFeeScalar >> 24) & 0xff);
    out.bytes[21] = static_cast<uint8_t>((operatorFeeScalar >> 16) & 0xff);
    out.bytes[22] = static_cast<uint8_t>((operatorFeeScalar >> 8) & 0xff);
    out.bytes[23] = static_cast<uint8_t>(operatorFeeScalar & 0xff);
    out.bytes[24] = static_cast<uint8_t>((operatorFeeConstant >> 56) & 0xff);
    out.bytes[25] = static_cast<uint8_t>((operatorFeeConstant >> 48) & 0xff);
    out.bytes[26] = static_cast<uint8_t>((operatorFeeConstant >> 40) & 0xff);
    out.bytes[27] = static_cast<uint8_t>((operatorFeeConstant >> 32) & 0xff);
    out.bytes[28] = static_cast<uint8_t>((operatorFeeConstant >> 24) & 0xff);
    out.bytes[29] = static_cast<uint8_t>((operatorFeeConstant >> 16) & 0xff);
    out.bytes[30] = static_cast<uint8_t>((operatorFeeConstant >> 8) & 0xff);
    out.bytes[31] = static_cast<uint8_t>(operatorFeeConstant & 0xff);
    return out;
}

void setOpFeeParams(state::test::InMemoryStateView& stateView)
{
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(31'250));
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(0));
    l1BlockAccount.storage[state::toEvmC(L1_FEE_SCALARS_SLOT)] = packFeeScalars(1, 0);
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'000'000, 5);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

void installAlwaysRevertRecipient(
    state::test::InMemoryStateView& stateView, evmc_address const& recipient)
{
    // PUSH1 0 PUSH1 0 REVERT
    stateView.insert_account(
        recipient, state::Account{.code = bcos::bytes{0x60, 0x00, 0x60, 0x00, 0xfd}});
}

OpStackMessageRequest make7702RevertInput(TestAuthKeyPair const& authKey, evmc_address sender,
    evmc_address recipient, evmc_address delegationTarget, uint64_t gasLimit,
    uint64_t authNonceForAuthority)
{
    OpStackMessageRequest input;
    input.message.kind = EVMC_CALL;
    input.message.gas = static_cast<int64_t>(gasLimit);
    input.message.sender = sender;
    input.message.recipient = recipient;
    input.message.code_address = recipient;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 1;
    input.blockInfo.chainId = 1;
    input.blockInfo.coinbase = addressFromLastByte(0x99);
    input.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    input.skipTransactionChecks = true;
    input.skipNonceChecks = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(authKey.sign(delegationTarget, authNonceForAuthority));
    return input;
}

void assertFailedIncluded7702Receipt(OpStackMessageResult const& output, uint64_t gasLimit)
{
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_NE(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::RevertInstruction));
    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    BOOST_CHECK_LT(output.gasUsed, static_cast<int64_t>(gasLimit));
}

void assertDelegationInstalled(
    state::StateDiff const& diff, evmc_address const& authority, uint64_t expectedNonce)
{
    auto const it = diff.accounts.find(authority);
    BOOST_REQUIRE(it != diff.accounts.end());
    auto const& installedCode = it->second.code;
    BOOST_REQUIRE_EQUAL(installedCode.size(), size_t(23));
    BOOST_CHECK_EQUAL(installedCode[0], 0xEF);
    BOOST_CHECK_EQUAL(installedCode[1], 0x01);
    BOOST_CHECK_EQUAL(installedCode[2], 0x00);
    BOOST_CHECK_EQUAL(it->second.nonce, expectedNonce);
}

OpStackMessageResult run7702RevertCase(TestAuthKeyPair const& authKey, evmc_address sender,
    evmc_address recipient, evmc_address delegationTarget, uint64_t authNonceForAuthority,
    bool preSeedAuthority)
{
    state::test::InMemoryStateView stateView;
    setOpFeeParams(stateView);
    installAlwaysRevertRecipient(stateView, recipient);

    stateView.insert_account(sender, state::Account{.balance = 10'000'000, .nonce = 0});

    auto const authority = authKey.address();
    if (preSeedAuthority &&
        std::memcmp(authority.bytes, sender.bytes, sizeof(authority.bytes)) != 0)
    {
        stateView.insert_account(authority, state::Account{.nonce = 0});
    }

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto input = make7702RevertInput(
        authKey, sender, recipient, delegationTarget, 200'000, authNonceForAuthority);
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;

    return task::syncWait(applyOpStackMessage(std::move(input)));
}
}  // namespace

// N6 / D3: op-geth included 7702 REVERT keeps failed receipt while auth survives revert.
BOOST_AUTO_TEST_CASE(included_self_sponsored_7702_revert_persists_auth_and_failed_receipt)
{
    auto const authKey = TestAuthKeyPair::generate();
    auto const sender = authKey.address();
    auto const recipient = addressFromLastByte(0x71);
    auto const delegationTarget = addressFromLastByte(0x72);

    auto const output = run7702RevertCase(authKey, sender, recipient, delegationTarget, 1, false);

    assertFailedIncluded7702Receipt(output, 200'000);
    assertDelegationInstalled(output.stateDiff, sender, 2);
}

BOOST_AUTO_TEST_CASE(included_separate_payer_7702_revert_persists_auth_on_authority)
{
    auto const authorityKey = TestAuthKeyPair::generate();
    auto const authority = authorityKey.address();
    auto const sender = addressFromLastByte(0x73);
    auto const recipient = addressFromLastByte(0x74);
    auto const delegationTarget = addressFromLastByte(0x75);

    auto const output =
        run7702RevertCase(authorityKey, sender, recipient, delegationTarget, 0, false);

    assertFailedIncluded7702Receipt(output, 200'000);
    assertDelegationInstalled(output.stateDiff, authority, 1);
}

BOOST_AUTO_TEST_CASE(included_7702_delegation_clearing_revert_persists_clear)
{
    auto const authKey = TestAuthKeyPair::generate();
    auto const sender = authKey.address();
    auto const recipient = addressFromLastByte(0x78);
    auto const previousTarget = addressFromLastByte(0x79);

    state::test::InMemoryStateView stateView;
    setOpFeeParams(stateView);
    installAlwaysRevertRecipient(stateView, recipient);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 10'000'000;
    senderAccount.code = addressToDelegation(previousTarget);
    stateView.insert_account(sender, std::move(senderAccount));

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto input = make7702RevertInput(authKey, sender, recipient, evmc_address{}, 200'000, 1);
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;

    auto const output = task::syncWait(applyOpStackMessage(std::move(input)));

    assertFailedIncluded7702Receipt(output, 200'000);
    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    BOOST_CHECK(it->second.code.empty());
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
    BOOST_CHECK(state::Bytes32Equal{}(it->second.codeHash, state::emptyCodeHash()));
}

}  // namespace bcos::evm::test
