#define BOOST_TEST_MODULE L1BlockGetterTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "state/InMemoryStateView.h"
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
}  // namespace

BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_l1block_getter)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0xa1);
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    state::Account l1Block;
    l1Block.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(123456));
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1Block));

    bytes input = {0x51, 0x9b, 0x4b, 0xd3};  // l1BaseFee()
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
    OpStackExecuteViaHostInput opInput;
    opInput.stateView = &stateView;
    opInput.vm = &vm;
    opInput.hashImpl = &hash;
    opInput.message = message;
    opInput.gasTipCap = 1;
    opInput.gasFeeCap = 2;
    opInput.blockInfo.baseFee = 1;
    opInput.txProps.warmDestination = true;

    auto output = task::syncWait(opStackExecuteViaHost(opInput));
    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.evmcResult.output_size, size_t(32));

    evmc_bytes32 raw{};
    std::copy(output.evmcResult.output_data, output.evmcResult.output_data + 32, raw.bytes);
    BOOST_CHECK_EQUAL(state::fromEvmC(raw), u256(123456));
}
}  // namespace bcos::evm::test
