#define BOOST_TEST_MODULE Bcos7823ModexpRejectTest

#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address modexpAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x05;
    return addr;
}

evmc_address senderAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x01;
    return addr;
}

bcos::bytes modexpHeaderBaseLen1025()
{
    bcos::bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    return input;
}

bcos::evm_standard::RevisionConfig osakaEthRevisionConfig()
{
    using Flag = bcos::ledger::Features::Flag;
    bcos::ledger::Features features;
    features.set(Flag::feature_evm_cancun);
    features.set(Flag::feature_evm_prague);
    features.set(Flag::feature_evm_osaka);
    features.set(Flag::feature_evm_eip2929);

    bcos::chain_policy::FiscoPolicy policy(features, false, false);
    bcostars::BlockHeader header;
    bcostars::protocol::BlockHeaderImpl headerImpl([&header]() { return std::addressof(header); });
    headerImpl.setNumber(1);
    headerImpl.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_2_VERSION));
    return policy.computeRevisionConfig(headerImpl).eth();
}
}  // namespace

BOOST_AUTO_TEST_CASE(innerExecute_feature_evm_osaka_modexp_field_1025_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = senderAddress();
    auto const modexp = modexpAddress();

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    auto const input = modexpHeaderBaseLen1025();
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = modexp;
    message.code_address = modexp;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    auto const revisionConfig = osakaEthRevisionConfig();
    BOOST_REQUIRE(revisionConfig.eip7823);
    BOOST_CHECK_EQUAL(revisionConfig.revision, EVMC_OSAKA);

    evmc::VM vm{evmc_create_evmone()};
    InnerExecuteInput execInput;
    execInput.state = &state;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig = revisionConfig;

    auto output = innerExecute(std::move(execInput));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_PRECOMPILE_FAILURE);
}

}  // namespace bcos::evm::test
