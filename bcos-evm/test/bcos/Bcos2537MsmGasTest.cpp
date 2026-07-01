#define BOOST_TEST_MODULE Bcos2537MsmGasTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/precompiled/Eip2537Gas.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}
}  // namespace

BOOST_AUTO_TEST_CASE(g1msm_k129_uses_max_discount_not_assert)
{
    // k=129 exceeds table length; EIP-2537 caps discount at entry for k=128 (519).
    BOOST_CHECK_EQUAL(bcos::evm::precompiled::blsG1MsmGas(129), 803412);
    BOOST_CHECK_EQUAL(bcos::evm::precompiled::blsG2MsmGas(129), 1520910);
}

BOOST_AUTO_TEST_CASE(g1msm_k200_uses_max_discount)
{
    BOOST_CHECK_EQUAL(bcos::evm::precompiled::blsG1MsmGas(200), 1245600);
    BOOST_CHECK_EQUAL(bcos::evm::precompiled::blsG2MsmGas(200), 2358000);
}

// fiscoExecute delegates to innerExecute for kernel precompiles; assert MSM gas on that path.
BOOST_AUTO_TEST_CASE(g1msm_k2_gas_matches_geth_via_innerExecute_prague)
{
    state::test::InMemoryStateView view;
    auto const sender = precompileAddress(0x01);
    auto const g1MsmAddr = precompileAddress(0x0c);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    bcos::bytes input(320, 0);
    int64_t const initialGas = 500'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = g1MsmAddr;
    message.code_address = g1MsmAddr;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    state::State state(view);
    InnerExecuteInput execInput;
    execInput.state = &state;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig.revision = EVMC_PRAGUE;
    execInput.revisionConfig.eip2537 = true;

    auto output = innerExecute(execInput);
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(initialGas - output.result.gas_left, 22776);
}

BOOST_AUTO_TEST_CASE(g1msm_k2_gas_matches_geth_via_innerExecute_isthmus_profile)
{
    state::test::InMemoryStateView view;
    auto const sender = precompileAddress(0x01);
    auto const g1MsmAddr = precompileAddress(0x0c);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    bcos::bytes input(320, 0);
    int64_t const initialGas = 500'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = g1MsmAddr;
    message.code_address = g1MsmAddr;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    state::State state(view);
    InnerExecuteInput execInput;
    execInput.state = &state;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();

    auto output = innerExecute(execInput);
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(initialGas - output.result.gas_left, 22776);
}

}  // namespace bcos::evm::test
