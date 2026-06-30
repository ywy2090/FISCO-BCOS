#define BOOST_TEST_MODULE L1AttributesDepositFailureTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/ApplyStateDiffToView.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <fstream>

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

bytes loadFixture(std::string_view name)
{
    auto const path = std::string(OPSTACK_FIXTURES_DIR) + "/" + std::string(name);
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(input.is_open(), "missing fixture: " << path);
    return {std::istreambuf_iterator<char>(input), {}};
}

OpStackExecutionRequest makeDepositInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    crypto::Hash const& hash, bytes const& calldata)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    OpStackExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx =
        OpStackDepositTx{.from = OP_DEPOSITOR_ACCOUNT, .to = OP_L1_BLOCK_PREDEPLOY, .gas = 500'000};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(failed_l1_attributes_deposit_does_not_commit_slot_changes)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto valid = loadFixture("isthmus_l1_attributes.bin");
    auto okOutput =
        task::syncWait(applyOpStackMessage(makeDepositInput(stateView, vm, hash, valid)));
    BOOST_REQUIRE_EQUAL(okOutput.evmcResult.status_code, EVMC_SUCCESS);
    applyStateDiffToView(okOutput.stateDiff, stateView);

    auto before = stateView.get_account(OP_L1_BLOCK_PREDEPLOY);
    BOOST_REQUIRE(before.has_value());

    bytes invalid = {0x09, 0x89, 0x99, 0xbe};
    auto failOutput =
        task::syncWait(applyOpStackMessage(makeDepositInput(stateView, vm, hash, invalid)));
    BOOST_REQUIRE_EQUAL(failOutput.evmcResult.status_code, EVMC_REVERT);
    BOOST_REQUIRE(failOutput.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*failOutput.receiptMeta.depositNonce, 1);
    BOOST_CHECK_GT(failOutput.gasUsed, 0);
    BOOST_CHECK_LT(failOutput.gasUsed, 500'000);

    auto const it = failOutput.stateDiff.accounts.find(OP_L1_BLOCK_PREDEPLOY);
    BOOST_CHECK(it == failOutput.stateDiff.accounts.end());
    auto const depositorIt = failOutput.stateDiff.accounts.find(OP_DEPOSITOR_ACCOUNT);
    BOOST_REQUIRE(depositorIt != failOutput.stateDiff.accounts.end());
    BOOST_CHECK_EQUAL(depositorIt->second.nonce, 2);
}
}  // namespace bcos::evm::test
