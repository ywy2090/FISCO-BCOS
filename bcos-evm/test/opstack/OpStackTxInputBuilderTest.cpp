#define BOOST_TEST_MODULE OpStackTxInputBuilderTest

#include "../../../transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h"
#include "bcos-evm/opstack/OpStackBlockHeaderExtension.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::evm;

namespace
{
bcostars::protocol::TransactionImpl makeWeb3Tx(bytes const& extra, uint8_t web3Kind)
{
    auto holder = std::make_shared<bcostars::Transaction>();
    holder->type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    holder->web3TypedTxKind = static_cast<tars::Char>(web3Kind);
    holder->extraTransactionBytes.assign(extra.begin(), extra.end());
    return bcostars::protocol::TransactionImpl([holder]() { return holder.get(); });
}

bytes buildDepositExtra()
{
    auto compact = [](u256 value) {
        auto encoded = toBigEndian(value);
        auto it = std::find_if(encoded.begin(), encoded.end(), [](auto c) { return c != 0; });
        if (it == encoded.end())
        {
            return bytes{};
        }
        return bytes(it, encoded.end());
    };

    bytes payload;
    h256 sourceHash{0x1234};
    evmc_address from{};
    from.bytes[19] = 0x11;
    evmc_address to{};
    to.bytes[19] = 0x22;
    bytes sourceHashRaw(sourceHash.begin(), sourceHash.end());
    bytes fromRaw(from.bytes, from.bytes + sizeof(from.bytes));
    bytes toRaw(to.bytes, to.bytes + sizeof(to.bytes));
    bytes mintData = compact(u256(0x33));
    bytes valueData = compact(u256(0x44));
    bytes callData{0xde, 0xad, 0xbe, 0xef};
    codec::rlp::encode(payload, sourceHashRaw, fromRaw, toRaw, mintData, valueData, uint64_t(21000),
        bytes{0x00}, callData);

    bytes extra;
    extra.push_back(bcos::executor::DEPOSIT_TX_TYPE);
    extra.insert(extra.end(), payload.begin(), payload.end());
    return extra;
}

bytes buildEip7702Extra()
{
    crypto::Secp256k1Crypto signatureImpl;
    auto keyPair = signatureImpl.generateKeyPair();

    u256 chainId = 1;
    uint64_t authNonce = 7;
    evmc_address delegateTo{};
    delegateTo.bytes[19] = 0x66;
    FixedBytes<20> delegateToFixed{bytesConstRef{delegateTo.bytes, sizeof(delegateTo.bytes)}};

    bytes authSignPayload;
    codec::rlp::encode(authSignPayload, chainId, delegateToFixed, authNonce);
    bytes authSignBytes;
    authSignBytes.push_back(0x05);
    authSignBytes.insert(authSignBytes.end(), authSignPayload.begin(), authSignPayload.end());
    auto authHash = crypto::keccak256Hash(ref(authSignBytes));
    auto signature = signatureImpl.sign(*keyPair, authHash, false);
    BOOST_REQUIRE(signature);

    bytes tuplePayload;
    bytes delegateRaw(delegateTo.bytes, delegateTo.bytes + sizeof(delegateTo.bytes));
    bytes r(signature->begin(), signature->begin() + 32);
    bytes s(signature->begin() + 32, signature->begin() + 64);
    uint64_t yParity = signature->at(64);
    codec::rlp::encode(tuplePayload, chainId, delegateRaw, authNonce, yParity, r, s);

    bytes authListEncoded;
    codec::rlp::encodeHeader(authListEncoded,
        {.isList = true, .payloadLength = static_cast<uint64_t>(tuplePayload.size())});
    authListEncoded.insert(authListEncoded.end(), tuplePayload.begin(), tuplePayload.end());

    bytes txItems;
    codec::rlp::encode(txItems, chainId);
    codec::rlp::encode(txItems, uint64_t(0));    // nonce
    codec::rlp::encode(txItems, uint64_t(1));    // maxPriorityFeePerGas
    codec::rlp::encode(txItems, uint64_t(100));  // maxFeePerGas
    codec::rlp::encode(txItems, uint64_t(50000));
    codec::rlp::encode(txItems, bytes{});  // to (empty)
    codec::rlp::encode(txItems, uint64_t(0));
    codec::rlp::encode(txItems, bytes{});
    txItems.push_back(codec::rlp::LIST_HEAD_BASE);  // empty access list
    txItems.insert(txItems.end(), authListEncoded.begin(), authListEncoded.end());

    bytes txPayload;
    codec::rlp::encodeHeader(
        txPayload, {.isList = true, .payloadLength = static_cast<uint64_t>(txItems.size())});
    txPayload.insert(txPayload.end(), txItems.begin(), txItems.end());

    bytes extra;
    extra.push_back(0x04);
    extra.insert(extra.end(), txPayload.begin(), txPayload.end());
    return extra;
}

bytes buildEip4844Extra()
{
    h256 blobHash{0xabcd};
    bytes blobHashRaw(blobHash.begin(), blobHash.end());

    bytes txItems;
    codec::rlp::encode(txItems, u256(10));     // chainId
    codec::rlp::encode(txItems, uint64_t(0));  // nonce
    codec::rlp::encode(txItems, uint64_t(1));  // maxPriorityFeePerGas
    codec::rlp::encode(txItems, uint64_t(2));  // maxFeePerGas
    codec::rlp::encode(txItems, uint64_t(50'000));
    evmc_address to{};
    to.bytes[19] = 0x55;
    bytes toRaw(to.bytes, to.bytes + sizeof(to.bytes));
    codec::rlp::encode(txItems, toRaw);
    codec::rlp::encode(txItems, uint64_t(0));  // value
    codec::rlp::encode(txItems, bytes{0x01, 0x02, 0x03});
    txItems.push_back(codec::rlp::LIST_HEAD_BASE);  // empty access list
    codec::rlp::encode(txItems, u256(30));          // maxFeePerBlobGas

    bytes blobListPayload;
    codec::rlp::encode(blobListPayload, blobHashRaw);
    bytes blobListEncoded;
    codec::rlp::encodeHeader(blobListEncoded,
        {.isList = true, .payloadLength = static_cast<uint64_t>(blobListPayload.size())});
    blobListEncoded.insert(blobListEncoded.end(), blobListPayload.begin(), blobListPayload.end());
    txItems.insert(txItems.end(), blobListEncoded.begin(), blobListEncoded.end());

    bytes txPayload;
    codec::rlp::encodeHeader(
        txPayload, {.isList = true, .payloadLength = static_cast<uint64_t>(txItems.size())});
    txPayload.insert(txPayload.end(), txItems.begin(), txItems.end());

    bytes extra;
    extra.push_back(0x03);
    extra.insert(extra.end(), txPayload.begin(), txPayload.end());
    return extra;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpStackTxInputBuilderTest)

BOOST_AUTO_TEST_CASE(decodes_deposit_extra_transaction_bytes)
{
    auto tx = makeWeb3Tx(buildDepositExtra(), bcos::executor::DEPOSIT_TX_TYPE);
    OpStackExecutionRequest input;
    opstack_tx::fillWeb3Fields(tx, input);

    BOOST_REQUIRE(input.depositTx.has_value());
    BOOST_CHECK_EQUAL(input.depositTx->gas, 21000);
    BOOST_CHECK_EQUAL(input.depositTx->value, u256(0x44));
    BOOST_REQUIRE(input.depositTx->mint.has_value());
    BOOST_CHECK_EQUAL(*input.depositTx->mint, u256(0x33));
    BOOST_REQUIRE(input.depositTx->to.has_value());
    BOOST_CHECK_EQUAL(input.depositTx->to->bytes[19], 0x22);
    BOOST_CHECK_EQUAL(input.depositTx->from.bytes[19], 0x11);
    BOOST_CHECK_EQUAL(input.message.sender.bytes[19], 0x11);
    BOOST_CHECK_EQUAL(input.depositTx->data.size(), 4U);
}

BOOST_AUTO_TEST_CASE(decodes_eip7702_authorization_from_extra_bytes)
{
    auto tx = makeWeb3Tx(buildEip7702Extra(), 0x04);
    OpStackExecutionRequest input;
    opstack_tx::fillWeb3Fields(tx, input);

    BOOST_CHECK(input.authorizationListPresent);
    BOOST_REQUIRE_EQUAL(input.authorizations.size(), 1U);
    BOOST_CHECK_EQUAL(input.authorizations[0].nonce, 7);
    BOOST_CHECK_EQUAL(input.authorizations[0].address.bytes[19], 0x66);
    BOOST_CHECK(!state::isZeroAddress(input.authorizations[0].authority));
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeSignedWeb3Tx(bcos::rpc::Web3Transaction w3)
{
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}

BOOST_AUTO_TEST_CASE(buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 10;
    w3.nonce = 42;
    w3.maxPriorityFeePerGas = 1'000'000'000;
    w3.maxFeePerGas = 2'000'000'000;
    w3.gasLimit = 21000;
    w3.to = bcos::Address("0x095ea7b32908fc5a31e5a3f3a21f4d1771440d0");
    w3.value = 0;
    w3.data = {0xde, 0xad, 0xbe, 0xef};
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 1;

    auto const unsignedBytes = w3.encodeForSign();
    bcos::bytes signedBytes;
    bcos::codec::rlp::encode(signedBytes, w3);
    auto tx = makeSignedWeb3Tx(std::move(w3));

    auto const fromUnsigned = newRollupCostData(bcos::ref(unsignedBytes));
    auto const fromSignedGolden = newRollupCostData(bcos::ref(signedBytes));
    auto const fromBuilder = opstack_tx::buildRollupCostData(*tx);

    BOOST_REQUIRE(fromBuilder.has_value());
    BOOST_CHECK_NE(fromUnsigned.fastLzSize, fromSignedGolden.fastLzSize);
    BOOST_CHECK_EQUAL(fromBuilder->fastLzSize, fromSignedGolden.fastLzSize);
    BOOST_CHECK_EQUAL(fromBuilder->ones, fromSignedGolden.ones);
    BOOST_CHECK_EQUAL(fromBuilder->zeroes, fromSignedGolden.zeroes);
}

BOOST_AUTO_TEST_CASE(buildRollupCostData_deposit_uses_extra_bytes_unchanged)
{
    auto depositExtra = buildDepositExtra();
    auto tx = makeWeb3Tx(depositExtra, bcos::executor::DEPOSIT_TX_TYPE);
    auto const direct = newRollupCostData(bcos::ref(depositExtra));
    auto const fromBuilder = opstack_tx::buildRollupCostData(tx);
    BOOST_REQUIRE(fromBuilder.has_value());
    BOOST_CHECK_EQUAL(fromBuilder->fastLzSize, direct.fastLzSize);
}

BOOST_AUTO_TEST_CASE(decodes_eip4844_blob_fields_from_extra_bytes)
{
    auto tx = makeWeb3Tx(buildEip4844Extra(), 0x03);
    OpStackExecutionRequest input;
    opstack_tx::fillWeb3Fields(tx, input);

    BOOST_CHECK_EQUAL(input.web3TypedTxKind, 0x03);
    BOOST_CHECK_EQUAL(input.blobGasFeeCap, u256(30));
    BOOST_REQUIRE_EQUAL(input.blobVersionedHashes.size(), 1U);
    BOOST_CHECK_EQUAL(input.blobVersionedHashes[0], h256(0xabcd));
}

BOOST_AUTO_TEST_CASE(resolve_fees_from_opf1_header)
{
    bcostars::protocol::BlockHeaderImpl header;
    evmc_address coinbase{};
    header.setExtraData(encodeOpStackHeaderExtra(coinbase, u256(5), 0));
    BOOST_CHECK_EQUAL(opstack_tx::resolveOpStackBaseFee(header), u256(5));
    BOOST_CHECK_EQUAL(opstack_tx::resolveOpStackBlobBaseFee(header), OP_MIN_BLOB_GAS_PRICE);
}

BOOST_AUTO_TEST_CASE(resolve_fees_throw_without_opf1_header)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setExtraData(bytes(20, 0x00));
    BOOST_CHECK_THROW(opstack_tx::resolveOpStackBaseFee(header), std::invalid_argument);
    BOOST_CHECK_THROW(opstack_tx::resolveOpStackBlobBaseFee(header), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
