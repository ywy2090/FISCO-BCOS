#define BOOST_TEST_MODULE L1BlockGetterTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

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

void runGetter(state::test::InMemoryStateView& stateView, bytes const& input, u256 const& expected)
{
    auto const sender = addressFromLastByte(0xa1);
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = input.data();
    message.input_size = input.size();

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    OpStackMessageRequest opInput;
    opInput.stateView = &stateView;
    opInput.vm = &vm;
    opInput.hashImpl = &hash;
    opInput.message = message;
    opInput.gasTipCap = 1;
    opInput.gasFeeCap = 2;
    opInput.blockInfo.baseFee = 1;

    auto output = task::syncWait(applyOpStackMessage(opInput));
    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.evmcResult.output_size, size_t(32));
    evmc_bytes32 raw{};
    std::copy(output.evmcResult.output_data, output.evmcResult.output_data + 32, raw.bytes);
    BOOST_CHECK_EQUAL(state::fromEvmC(raw), expected);
}
}  // namespace

BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_l1block_getter)
{
    state::test::InMemoryStateView stateView;
    state::Account l1Block;
    l1Block.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(123456));
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    bytes input = {0x51, 0x9b, 0x4b, 0xd3};  // l1BaseFee()
    runGetter(stateView, input, u256(123456));
}

BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_basefee_getter)
{
    state::test::InMemoryStateView stateView;
    state::Account l1Block;
    l1Block.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(123456));
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    bytes input = {0x5c, 0xf2, 0x49, 0x69};  // basefee()
    runGetter(stateView, input, u256(123456));
}

BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_number_getter)
{
    state::test::InMemoryStateView stateView;
    state::Account l1Block;
    evmc_bytes32 numberTs{};
    for (size_t i = 0; i < 8; ++i)
    {
        numberTs.bytes[24 + i] = static_cast<uint8_t>(0x21 + i);
    }
    l1Block.storage[state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT)] = numberTs;
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    bytes input = {0x83, 0x81, 0xf5, 0x8a};  // number()
    runGetter(stateView, input, u256(0x2122232425262728ULL));
}
}  // namespace bcos::evm::test
