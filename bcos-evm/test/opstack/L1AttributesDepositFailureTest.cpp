#define BOOST_TEST_MODULE L1AttributesDepositFailureTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/ApplyStateDiffToView.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>
#include <evmone/evmone.h>
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

OpStackExecuteViaHostInput makeDepositInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
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

    OpStackExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{
        .from = OP_DEPOSITOR_ACCOUNT, .to = OP_L1_BLOCK_PREDEPLOY, .gas = 500'000};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(failed_l1_attributes_deposit_does_not_commit_slot_changes)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto valid = loadFixture("isthmus_l1_attributes.bin");
    auto okOutput = task::syncWait(opStackExecuteViaHost(makeDepositInput(stateView, vm, hash, valid)));
    BOOST_REQUIRE_EQUAL(okOutput.evmcResult.status_code, EVMC_SUCCESS);
    applyStateDiffToView(okOutput.stateDiff, stateView);

    auto before = stateView.get_account(OP_L1_BLOCK_PREDEPLOY);
    BOOST_REQUIRE(before.has_value());

    bytes invalid = {0x09, 0x89, 0x99, 0xbe};
    auto failOutput = task::syncWait(opStackExecuteViaHost(makeDepositInput(stateView, vm, hash, invalid)));
    BOOST_REQUIRE_EQUAL(failOutput.evmcResult.status_code, EVMC_REVERT);

    auto const it = failOutput.stateDiff.accounts.find(OP_L1_BLOCK_PREDEPLOY);
    BOOST_CHECK(it == failOutput.stateDiff.accounts.end());
}
}  // namespace bcos::evm::test
