#define BOOST_TEST_MODULE OpStackTxInputBuilderTest

#include "../../../transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpStackTxInputBuilderTest)

BOOST_AUTO_TEST_CASE(decodes_deposit_extra_transaction_bytes)
{
    auto tx = makeWeb3Tx(buildDepositExtra(), bcos::executor::DEPOSIT_TX_TYPE);
    OpStackExecuteViaHostInput input;
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
    OpStackExecuteViaHostInput input;
    opstack_tx::fillWeb3Fields(tx, input);

    BOOST_CHECK(input.authorizationListPresent);
    BOOST_REQUIRE_EQUAL(input.authorizations.size(), 1U);
    BOOST_CHECK_EQUAL(input.authorizations[0].nonce, 7);
    BOOST_CHECK_EQUAL(input.authorizations[0].address.bytes[19], 0x66);
    BOOST_CHECK(!state::isZeroAddress(input.authorizations[0].authority));
}

BOOST_AUTO_TEST_SUITE_END()
