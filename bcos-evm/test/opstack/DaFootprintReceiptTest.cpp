#define BOOST_TEST_MODULE DaFootprintReceiptTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
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
    crypto::HashType hash(bytesConstRef) const override { return crypto::HashType{}; }
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

// 跑 Jovian L1 attributes deposit，把 daFootprintGasScalar=400 写入 L1Block slot。
void seedJovianL1Block(state::test::InMemoryStateView& stateView, evmc::VM& vm, crypto::Hash& hash)
{
    stateView.insert_account(OP_DEPOSITOR_ACCOUNT, state::Account{.nonce = 0});
    auto calldata = loadFixture("jovian_l1_attributes.bin");
    evmc_message m{};
    m.kind = EVMC_CALL;
    m.gas = 500'000;
    m.sender = OP_DEPOSITOR_ACCOUNT;
    m.recipient = OP_L1_BLOCK_PREDEPLOY;
    m.code_address = OP_L1_BLOCK_PREDEPLOY;
    m.input_data = calldata.data();
    m.input_size = calldata.size();

    OpStackMessageRequest in;
    in.stateView = &stateView;
    in.vm = &vm;
    in.hashImpl = &hash;
    in.message = m;
    in.blockInfo.baseFee = 1;
    in.gasTipCap = 1;
    in.gasFeeCap = 1;
    in.forkSchedule = makeJovianPlusForkSchedule();
    in.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    in.depositTx =
        OpStackDepositTx{.from = OP_DEPOSITOR_ACCOUNT, .to = OP_L1_BLOCK_PREDEPLOY, .gas = 500'000};
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);
    applyStateDiffToView(out.stateDiff, stateView);
}

OpStackMessageRequest makeUserTx(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    crypto::Hash& hash, OpStackForkSchedule schedule)
{
    auto const user = addressFromLastByte(0x71);
    auto const target = addressFromLastByte(0x72);
    stateView.insert_account(
        user, state::Account{.balance = u256("1000000000000000000000000000000"), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    evmc_message m{};
    m.kind = EVMC_CALL;
    m.gas = 100'000;
    m.sender = user;
    m.recipient = target;
    m.code_address = target;

    OpStackMessageRequest in;
    in.stateView = &stateView;
    in.vm = &vm;
    in.hashImpl = &hash;
    in.message = m;
    in.blockInfo.baseFee = 1;
    in.gasTipCap = 1;
    in.gasFeeCap = 2;
    in.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
    in.txProps.warmDestination = true;
    in.forkSchedule = schedule;
    in.rollupCostData = RollupCostData{.ones = 8, .fastLzSize = 200};
    return in;
}
}  // namespace

// Jovian + 非 deposit：恒写；scalar=400、fastLzSize=200 -> estimatedDASize=124 -> footprint=49600
BOOST_AUTO_TEST_CASE(jovian_user_tx_writes_da_footprint)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    seedJovianL1Block(stateView, vm, hash);

    auto in = makeUserTx(stateView, vm, hash, makeJovianPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_REQUIRE(out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprintGasScalar, 400u);
    BOOST_REQUIRE(out.receiptMeta.daFootprint.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprint, 124u * 400u);
}

// pre-Jovian（Isthmus 默认）：不写
BOOST_AUTO_TEST_CASE(pre_jovian_user_tx_omits_da_footprint)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto in = makeUserTx(stateView, vm, hash, makeIsthmusPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_CHECK(!out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK(!out.receiptMeta.daFootprint.has_value());
}

// Jovian 但 L1Block 未写 scalar（=0）：恒写 0
BOOST_AUTO_TEST_CASE(jovian_zero_scalar_writes_zero)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto in = makeUserTx(stateView, vm, hash, makeJovianPlusForkSchedule());
    auto out = task::syncWait(applyOpStackMessage(in));
    BOOST_REQUIRE_EQUAL(out.evmcResult.status_code, EVMC_SUCCESS);

    BOOST_REQUIRE(out.receiptMeta.daFootprintGasScalar.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprintGasScalar, 0u);
    BOOST_REQUIRE(out.receiptMeta.daFootprint.has_value());
    BOOST_CHECK_EQUAL(*out.receiptMeta.daFootprint, 0u);
}
}  // namespace bcos::evm::test
