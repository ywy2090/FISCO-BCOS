#define BOOST_TEST_MODULE L1AttributesDepositTest

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

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(l1_attributes_deposit_updates_l1block_and_affects_following_user_tx)
{
    state::test::InMemoryStateView stateView;
    auto const user = addressFromLastByte(0x71);
    auto const target = addressFromLastByte(0x72);
    stateView.insert_account(
        user, state::Account{.balance = u256("1000000000000000000000000000000"), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    auto calldata = loadFixture("isthmus_l1_attributes.bin");
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    evmc_message depositMessage{};
    depositMessage.kind = EVMC_CALL;
    depositMessage.gas = 500'000;
    depositMessage.sender = OP_DEPOSITOR_ACCOUNT;
    depositMessage.recipient = OP_L1_BLOCK_PREDEPLOY;
    depositMessage.code_address = OP_L1_BLOCK_PREDEPLOY;
    depositMessage.input_data = calldata.data();
    depositMessage.input_size = calldata.size();

    OpStackExecuteViaHostInput depositInput;
    depositInput.stateView = &stateView;
    depositInput.vm = &vm;
    depositInput.hashImpl = &hash;
    depositInput.message = depositMessage;
    depositInput.blockInfo.baseFee = 1;
    depositInput.gasTipCap = 1;
    depositInput.gasFeeCap = 1;
    depositInput.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    depositInput.depositTx = OpStackDepositTx{
        .from = OP_DEPOSITOR_ACCOUNT, .to = OP_L1_BLOCK_PREDEPLOY, .gas = 500'000};

    auto depositOutput = task::syncWait(opStackExecuteViaHost(depositInput));
    BOOST_REQUIRE_EQUAL(depositOutput.evmcResult.status_code, EVMC_SUCCESS);
    applyStateDiffToView(depositOutput.stateDiff, stateView);

    evmc_message userMessage{};
    userMessage.kind = EVMC_CALL;
    userMessage.gas = 100'000;
    userMessage.sender = user;
    userMessage.recipient = target;
    userMessage.code_address = target;

    OpStackExecuteViaHostInput userInput;
    userInput.stateView = &stateView;
    userInput.vm = &vm;
    userInput.hashImpl = &hash;
    userInput.message = userMessage;
    userInput.blockInfo.baseFee = 1;
    userInput.gasTipCap = 1;
    userInput.gasFeeCap = 2;
    userInput.txProps.warmDestination = true;
    userInput.opTxExecutor.m_isIsthmus = true;
    userInput.rollupCostData = RollupCostData{.ones = 8, .fastLzSize = 64};

    auto userOutput = task::syncWait(opStackExecuteViaHost(userInput));
    BOOST_REQUIRE_EQUAL(userOutput.evmcResult.status_code, EVMC_SUCCESS);
    auto const it = userOutput.stateDiff.accounts.find(OP_L1_FEE_RECIPIENT);
    BOOST_REQUIRE(it != userOutput.stateDiff.accounts.end());
    BOOST_CHECK_GT(it->second.balance, u256(0));
}
}  // namespace bcos::evm::test
