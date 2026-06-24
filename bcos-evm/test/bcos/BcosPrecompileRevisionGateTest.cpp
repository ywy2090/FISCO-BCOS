#define BOOST_TEST_MODULE BcosPrecompileRevisionGateTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoExecutionBridge.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
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

evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}
}  // namespace

BOOST_AUTO_TEST_CASE(isActivePrecompile_cancun_rejects_prague_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};
    auto const blsAddr = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_CANCUN, cfg, blsAddr));
}

BOOST_AUTO_TEST_CASE(fiscoExecute_cancun_call_0x0b_not_precompile_dispatch)
{
    state::test::InMemoryStateView view;
    auto const sender = precompileAddress(0x01);
    auto const blsAddr = precompileAddress(0x0b);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    int64_t const initialGas = 200'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = blsAddr;
    message.code_address = blsAddr;

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    FiscoExecutionRequest input;
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.eth().revision = EVMC_CANCUN;

    auto output = task::syncWait(fiscoExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, initialGas - BALANCE_TRANSFER_GAS);
    BOOST_CHECK_EQUAL(output.evmcResult.output_size, size_t(0));
}

}  // namespace bcos::evm::test
