/*
 * EIP-1559: ethReferenceExecute normalizes gasPrice before EVM so GASPRICE matches geth.
 */
#define BOOST_TEST_MODULE EthExecuteViaEth1559GasPriceTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/apply/ApplyReferenceMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
using bcos::u256;

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig london1559Config()
{
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_LONDON;
    cfg.eip1559 = true;
    cfg.eip2929 = true;
    return cfg;
}

// GASPRICE BASEFEE ADD PUSH1 0 MSTORE PUSH1 32 PUSH1 0 RETURN
bcos::bytes const kGasPriceBaseFeeSumBytecode = bcos::fromHex("3a480160005260206000f3");
}  // namespace

BOOST_AUTO_TEST_SUITE(EthExecuteViaEth1559GasPriceTest)

BOOST_AUTO_TEST_CASE(applyReferenceMessage_type2_normalizes_gas_price_for_gasprice_opcode)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = kGasPriceBaseFeeSumBytecode;
    view.insert_account(target, targetAccount);

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;
    blockInfo.baseFee = 10;

    EthReferenceRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = london1559Config();
    input.gasPrice = 100;  // wrong pre-normalization placeholder
    input.gasTipCap = 2;
    input.gasFeeCap = 100;
    input.web3TypedTxKind = 0x02;
    input.hasExplicitFeeCaps = true;

    auto output = task::syncWait(applyReferenceMessage(std::move(input)));

    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.evmcResult.output_size, size_t(32));
    evmc_bytes32 raw{};
    std::copy(output.evmcResult.output_data, output.evmcResult.output_data + 32, raw.bytes);
    // effective = min(gasFeeCap, gasTipCap + baseFee) = min(100, 12) = 12; sum with baseFee = 22
    BOOST_CHECK_EQUAL(state::fromEvmC(raw), u256(22));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
