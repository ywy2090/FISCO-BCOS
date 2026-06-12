/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Web3TypeTest.cpp
 * @author: kyonGuo
 * @date 2024/4/9
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::codec::rlp;
namespace bcos::test
{
static const std::vector<AccessListEntry> s_accessList{
    {Address("0xde0b295669a9fd93d5f28d9ec85e40f4cb697bae"),
        {
            HashType("0x0000000000000000000000000000000000000000000000000000000000000003"),
            HashType("0x0000000000000000000000000000000000000000000000000000000000000007"),
        }},
    {Address("0xbb9bc244d798123fde783fcc1c72d3bb8c189413"), {}},
};

static bcos::bytes const kDefaultSignatureR =
    HashType("0x36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0").asBytes();
static bcos::bytes const kDefaultSignatureS =
    HashType("0x5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094").asBytes();

static void applyDefaultSignature(Web3Transaction& tx, uint64_t yParity = 0)
{
    tx.signatureV = yParity;
    tx.signatureR = kDefaultSignatureR;
    tx.signatureS = kDefaultSignatureS;
}

static void normalizeSignatureForCompare(Web3Transaction& tx)
{
    if (tx.signatureR.size() < crypto::SECP256K1_SIGNATURE_R_LEN)
    {
        tx.signatureR.insert(
            tx.signatureR.begin(), crypto::SECP256K1_SIGNATURE_R_LEN - tx.signatureR.size(), 0);
    }
    if (tx.signatureS.size() < crypto::SECP256K1_SIGNATURE_S_LEN)
    {
        tx.signatureS.insert(tx.signatureS.begin(),
            crypto::SECP256K1_SIGNATURE_S_LEN - tx.signatureS.size(), bcos::byte(0));
    }
}

static void assertWeb3TransactionsEqual(
    Web3Transaction const& expected, Web3Transaction const& actual)
{
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(expected.type), static_cast<uint8_t>(actual.type));
    BOOST_CHECK_EQUAL(expected.chainId.has_value(), actual.chainId.has_value());
    if (expected.chainId.has_value())
    {
        BOOST_CHECK_EQUAL(expected.chainId.value(), actual.chainId.value());
    }
    BOOST_CHECK_EQUAL(expected.nonce, actual.nonce);
    BOOST_CHECK_EQUAL(expected.gasLimit, actual.gasLimit);
    BOOST_CHECK_EQUAL(expected.to.has_value(), actual.to.has_value());
    if (expected.to.has_value())
    {
        BOOST_CHECK_EQUAL(expected.to.value(), actual.to.value());
    }
    BOOST_CHECK_EQUAL(expected.value, actual.value);
    BOOST_CHECK_EQUAL(toHex(expected.data), toHex(actual.data));
    BOOST_CHECK_EQUAL(expected.maxFeePerGas, actual.maxFeePerGas);
    BOOST_CHECK_EQUAL(expected.maxPriorityFeePerGas, actual.maxPriorityFeePerGas);
    BOOST_CHECK(expected.accessList == actual.accessList);
    BOOST_CHECK_EQUAL(expected.maxFeePerBlobGas, actual.maxFeePerBlobGas);
    BOOST_REQUIRE_EQUAL(expected.blobVersionedHashes.size(), actual.blobVersionedHashes.size());
    for (size_t i = 0; i < expected.blobVersionedHashes.size(); ++i)
    {
        BOOST_CHECK_EQUAL(
            toHex(expected.blobVersionedHashes[i]), toHex(actual.blobVersionedHashes[i]));
    }
    BOOST_CHECK_EQUAL(expected.signatureV, actual.signatureV);
    BOOST_CHECK_EQUAL(toHex(expected.signatureR), toHex(actual.signatureR));
    BOOST_CHECK_EQUAL(toHex(expected.signatureS), toHex(actual.signatureS));
}

static void assertEncodeDecodeRoundtrip(Web3Transaction const& original)
{
    Web3Transaction expected = original;
    normalizeSignatureForCompare(expected);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, original);
    BOOST_REQUIRE(!encoded.empty());

    auto const originalHash = original.txHash();
    BOOST_CHECK_EQUAL(originalHash, bcos::crypto::keccak256Hash(bcos::ref(encoded)));

    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_REQUIRE_MESSAGE(e == nullptr, e ? e->errorMessage() : "decode failed");

    normalizeSignatureForCompare(decoded);
    assertWeb3TransactionsEqual(expected, decoded);
    BOOST_CHECK_EQUAL(decoded.txHash(), originalHash);

    bcos::bytes reEncoded{};
    codec::rlp::encode(reEncoded, decoded);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(encoded), toHexStringWithPrefix(reEncoded));
}

static void assertDecodeFails(bcos::bytes input, DecodingError expectedError)
{
    auto bRef = bcos::ref(input);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_REQUIRE(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), expectedError);
}

static void assertDecodeFromPayloadFails(bcos::bytes input, DecodingError expectedError)
{
    auto bRef = bcos::ref(input);
    Web3Transaction tx{};
    auto e = codec::rlp::decodeFromPayload(bRef, tx);
    BOOST_REQUIRE(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), expectedError);
}

static bcos::bytes injectExtraListElement(bcos::bytes encoded)
{
    if (encoded.size() >= 2 && encoded[0] == 0xf8)
    {
        encoded.push_back(0x80);
        encoded[1] = static_cast<bcos::byte>(encoded[1] + 1);
        return encoded;
    }
    if (!encoded.empty() && encoded[0] >= 0xc0 && encoded[0] <= 0xf7)
    {
        encoded.push_back(0x80);
        encoded[0] = static_cast<bcos::byte>(encoded[0] + 1);
        return encoded;
    }
    return encoded;
}

static Web3Transaction makeLegacySignedTx(bool withChainId, bool contractCreation)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::Legacy;
    tx.nonce = 42;
    tx.maxFeePerGas = 20000000000;
    tx.maxPriorityFeePerGas = 20000000000;
    tx.gasLimit = 21000;
    if (!contractCreation)
    {
        tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    }
    tx.value = 1000000000000000000ull;
    tx.data = fromHex("a9059cbb");
    if (withChainId)
    {
        tx.chainId = 1;
        applyDefaultSignature(tx, 1);
    }
    else
    {
        tx.chainId = std::nullopt;
        applyDefaultSignature(tx, 0);
    }
    return tx;
}

static Web3Transaction makeEIP2930SignedTx()
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP2930;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 30000000000;
    tx.gasLimit = 5748100;
    tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    tx.value = 2000000000000000000ull;
    tx.data = fromHex("6ebaf477f83e051589c1188bcc6ddccd");
    tx.accessList = s_accessList;
    applyDefaultSignature(tx, 0);
    return tx;
}

static Web3Transaction makeEIP1559SignedTx(bool contractCreation)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 10000000000;
    tx.gasLimit = contractCreation ? 22000000 : 5748100;
    if (!contractCreation)
    {
        tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    }
    tx.value = contractCreation ? 0 : 2000000000000000000ull;
    tx.data = contractCreation ? fromHex(
                                     "6080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d"
                                     "31126b724d508df287783584"
                                     "9e8744017ab36a9b47f905f90a1") :
                                 fromHex("6ebaf477f83e051589c1188bcc6ddccd");
    tx.accessList = contractCreation ? std::vector<AccessListEntry>{} : s_accessList;
    applyDefaultSignature(tx, 0);
    return tx;
}

static Web3Transaction makeEIP4844SignedTx()
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP4844;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 10000000000;
    tx.gasLimit = 5748100;
    tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    tx.value = 0;
    tx.data = fromHex("04f7");
    tx.accessList = s_accessList;
    tx.maxFeePerBlobGas = 123;
    tx.blobVersionedHashes = {
        HashType("0xc6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210"),
        HashType("0x8aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab736"),
    };
    applyDefaultSignature(tx, 1);
    return tx;
}

BOOST_FIXTURE_TEST_SUITE(testWeb3Type, RPCFixture)
BOOST_AUTO_TEST_CASE(testLegacyTransactionDecode)
{
    // clang-format off
    constexpr std::string_view  rawTx = "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    // constexpr std::string_view  rawTx = "0xf8ac82017c8504a817c800835fefd89409d07ecb4d6f32e91503c04b192e3bdeb7f857f480b8442c7128d700000000000000000000000000000000000000000000000000009bc24e89949a00000000000000000000000000000000000000000000000000000000000000001ba0cd372eb41b6b4e9e576233bb29c1492e0329fac1331f492a69e4a1b586a1a28ba032950cc4184ca8b0d45b24d13345157b4153d7ccc0d187dbab018be07726d186";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(!e);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 12);
    BOOST_CHECK_EQUAL(tx.nonce, 12);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 21000);
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a"));
    BOOST_CHECK_EQUAL(tx.value, 10000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data),
        "a9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc9"
        "71600000000000000000000015af1d78b58c4000");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "2d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35 + 1);

    auto hash = tx.txHash().hexPrefixed();
    BOOST_CHECK_EQUAL(hash, bcos::crypto::keccak256Hash(ref(bytes)).hexPrefixed());
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testConstructTx)
{
    Web3Transaction testTx;
    testTx.value = 1000000000000000000;
    testTx.type = rpc::TransactionType::Legacy;
    testTx.data = {};
    testTx.to = Address("0x1e58529dAA467406645d0f4B63dec96CA0b87d70");
    testTx.nonce = 19;
    testTx.gasLimit = 210000;
    testTx.maxFeePerGas = 20000000000;
    testTx.maxPriorityFeePerGas = 20000000000;
    testTx.chainId = 31337;

    auto signData = testTx.encodeForSign();
    std::string priv = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    auto key = std::make_shared<KeyImpl>(fromHex(priv));
    auto newHash = crypto::keccak256Hash(ref(signData));
    auto signatureImpl = bcos::crypto::Secp256k1Crypto();
    auto keyPair = std::make_unique<Secp256k1KeyPair>(key);

    auto signature = signatureImpl.sign(*keyPair, newHash, false);
    auto [_, addr] = signatureImpl.recoverAddress(*hashImpl, newHash, ref(*signature));
    auto newAddr = toHex(addr);
    BOOST_CHECK_EQUAL(newAddr, Address("C96aAa54E2d44c299564da76e1cD3184A2386B8D").hex());
    testTx.signatureR = {signature->begin(), signature->begin() + 32};
    testTx.signatureS = {signature->begin() + 32, signature->begin() + 64};
    testTx.signatureV = signature->back();
    bcos::bytes toData;
    bcos::codec::rlp::encode(toData, testTx);
    auto const newTx = toHexStringWithPrefix(toData);
    BOOST_CHECK(!newTx.empty());
}

BOOST_AUTO_TEST_CASE(testEIP2930Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP2930);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testEIP1559Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 2000000000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "6ebaf477f83e051589c1188bcc6ddccd");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}
BOOST_AUTO_TEST_CASE(testEIP1559Transaction2)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x02f90129824ee8090a82012c84014fb1808080b8d46080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e8744017ab36a9b47f905f90a160928060425f395ff3fe6080604052348015600e575f5ffd5b50600436106026575f3560e01c80637b0cb83914602a575b5f5ffd5b60306032565b005b6040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e8744017ab36a9b47f905f90a156fea2646970667358221220b26bf8d47ffaa4c5ffecf6303ac218970d8ab50724943980b859fc2ac8e384e164736f6c634300081c0033c080a0f0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693a053ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 20200);
    BOOST_CHECK_EQUAL(tx.nonce, 9);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 300);
    BOOST_CHECK_EQUAL(tx.gasLimit, 22000000);
    BOOST_CHECK_EQUAL(tx.to.has_value(), false);
    BOOST_CHECK_EQUAL(tx.value, 0);
    BOOST_CHECK_EQUAL(toHex(tx.data),
        "6080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e874"
        "4017ab36a9b47f905f90a160928060425f395ff3fe6080604052348015600e575f5ffd5b50600436106026575f"
        "3560e01c80637b0cb83914602a575b5f5ffd5b60306032565b005b6040517fcf16a92280c1bbb43f72d31126b7"
        "24d508df2877835849e8744017ab36a9b47f905f90a156fea2646970667358221220b26bf8d47ffaa4c5ffecf6"
        "303ac218970d8ab50724943980b859fc2ac8e384e164736f6c634300081c0033");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "f0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "53ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd");

    BOOST_CHECK_EQUAL(tx.sender(), "0x2a09be8823b80f337170650802d1a0f8a99fe2d8");
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0x1c4af2f7b65cc5c589aced8a9e0965183636d718f6fcdab3322b538710d22995");
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(testEIP4844Transaction)
{
    // clang-format off
    constexpr std::string_view rawTx = "0x03f9012705078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7808204f7f872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c07bf842a0c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210a08aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab73601a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 5);
    BOOST_CHECK_EQUAL(tx.nonce, 7);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(tx.gasLimit, 5748100);
    BOOST_CHECK_EQUAL(tx.to.value(), Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"));
    BOOST_CHECK_EQUAL(tx.value, 0);
    BOOST_CHECK_EQUAL(toHex(tx.data), "04f7");
    BOOST_CHECK_EQUAL(tx.maxFeePerBlobGas, 123);
    BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 2);
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[0]),
        "c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[1]),
        "8aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab736");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35 + 1);
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureR), "36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0");
    BOOST_CHECK_EQUAL(
        toHex(tx.signatureS), "5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094");
    BOOST_CHECK(tx.accessList == s_accessList);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);
}

BOOST_AUTO_TEST_CASE(recoverAddress)
{
    // clang-format off
    constexpr std::string_view  rawTx = "0xf8ac82017c8504a817c800835fefd89409d07ecb4d6f32e91503c04b192e3bdeb7f857f480b8442c7128d700000000000000000000000000000000000000000000000000009bc24e89949a00000000000000000000000000000000000000000000000000000000000000001ba0cd372eb41b6b4e9e576233bb29c1492e0329fac1331f492a69e4a1b586a1a28ba032950cc4184ca8b0d45b24d13345157b4153d7ccc0d187dbab018be07726d186";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(!e);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0xec5e7dec9d2d6bfa1f2221ace01ae3deb6906fb0");
}

BOOST_AUTO_TEST_CASE(EIP1559Recover)
{
    // https://etherscan.io/tx/0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc
    // clang-format off
    constexpr std::string_view rawTx = "0x02f8720183015b148085089a36ae8682520894e10f39a0dfb9e380b6d176eb7183af32b68028d78806e9ba3bd88b600080c080a032ab966d1c9cc2be6952713a1599a95a14f6e92c9f62d7fa40aa62d8b764ffcfa060bdbe7b8e66a0c681a90d4da0c7c0a4ba9321d49fc5c65bfddb847539e35d56";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 88852);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 0);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 36947013254u);
    BOOST_CHECK_EQUAL(tx.gasLimit, 21000);
    BOOST_CHECK_EQUAL(tx.to.value().hexPrefixed(), "0xe10f39a0dfb9e380b6d176eb7183af32b68028d7");
    BOOST_CHECK_EQUAL(tx.value, 498134000000000000ull);
    BOOST_CHECK_EQUAL(toHex(tx.data), "");
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 35);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc");
    auto txHash = bcos::crypto::keccak256Hash(ref(bytes)).hexPrefixed();
    BOOST_CHECK_EQUAL(txHash, "0xcf6b53ec88659fc86e854af2e8453fa519ca261f949ef291e33c5f44ead870dc");
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0x595063172c85b1e8ac2fe74fcb6b7dc26844cc2d");
}

BOOST_AUTO_TEST_CASE(EIP4844Recover)
{
    // https://etherscan.io/tx/0x8bb97c1480b533396b0940a0f94ef5974c4989954f52d928e06e38d363bbd560
    // clang-format off
    constexpr std::string_view rawTx = "0x03f9049f0183082ef8843b9aca008537942bfdb083036a2b941c479675ad559dc151f6ec7ed3fbf8cee79582b680b8a43e5aa082000000000000000000000000000000000000000000000000000000000008f7060000000000000000000000000000000000000000000000000000000000168763000000000000000000000000e64a54e2533fd126c2e452c5fab544d80e2e4eb5000000000000000000000000000000000000000000000000000000000a8cc7c7000000000000000000000000000000000000000000000000000000000a8ccabef902c0f8dd941c479675ad559dc151f6ec7ed3fbf8cee79582b6f8c6a00000000000000000000000000000000000000000000000000000000000000000a00000000000000000000000000000000000000000000000000000000000000001a0000000000000000000000000000000000000000000000000000000000000000aa0b53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103a0360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbca0a10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660f90141948315177ab297ba92a06054ce80a67ed4dbd7ed3af90129a00000000000000000000000000000000000000000000000000000000000000006a00000000000000000000000000000000000000000000000000000000000000007a00000000000000000000000000000000000000000000000000000000000000009a0000000000000000000000000000000000000000000000000000000000000000aa0b53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103a0360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbca0a66cc928b5edb82af9bd49922954155ab7b0942694bea4ce44661d9a873fbd8da0a66cc928b5edb82af9bd49922954155ab7b0942694bea4ce44661d9a873fbd8ea0f652222313e28459528d920b65115c16c04f3efc82aaedc97be59f3f379294a1f89b94e64a54e2533fd126c2e452c5fab544d80e2e4eb5f884a00000000000000000000000000000000000000000000000000000000000000004a00000000000000000000000000000000000000000000000000000000000000005a0e85fd79f89ff278fc57d40aecb7947873df9f0beac531c8f71a98f630e1eab62a07686888b19bb7b75e46bb1aa328b65150743f4899443d722f0adf8e252ccda410af8c6a001f8198b33db3461035e1621dd12498e57cf26efe9578b39054fbe5efdf83032a00152295a881b358db5dcf58b54661ee60f595de7f57eb93030a5d9e57bcae30ea0014ea3a3d4fc547ccb6974c5c4deb7778b755b0b3d56be88c54ef3a39d209b4ca001b378a4a2a4a3806740ec38b5672d213c78bbcae34550d014a265fc262fe06ea001b83eca80127748b71bcaa6a8c9edbfd5a9fb47933032891c27e07668f48867a001904e6186ecd84f6897659777846d5510bfbeb2863a93d8432f0fcf89c3e2c901a028bc2660c742d25de1f9af5550bfb734ac81c1e0d703c285447684872430635aa01788719406012ded6dd859a3a0218cb0acccd3f30a93da6796abc19ba3192fcf";
    // clang-format on
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::EIP4844);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.nonce, 536312);
    BOOST_CHECK_EQUAL(tx.maxPriorityFeePerGas, 1000000000);
    BOOST_CHECK_EQUAL(tx.maxFeePerGas, 238709112240);
    BOOST_CHECK_EQUAL(tx.gasLimit, 223787);
    BOOST_CHECK_EQUAL(tx.to.value().hexPrefixed(), "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6");
    BOOST_CHECK_EQUAL(tx.value, 0);
    // clang-format off
    BOOST_CHECK_EQUAL(toHex(tx.data), "3e5aa082000000000000000000000000000000000000000000000000000000000008f7060000000000000000000000000000000000000000000000000000000000168763000000000000000000000000e64a54e2533fd126c2e452c5fab544d80e2e4eb5000000000000000000000000000000000000000000000000000000000a8cc7c7000000000000000000000000000000000000000000000000000000000a8ccabe");
    // clang-format on
    BOOST_CHECK_EQUAL(tx.getSignatureV(), tx.chainId.value() * 2 + 36);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(),
        "0x8bb97c1480b533396b0940a0f94ef5974c4989954f52d928e06e38d363bbd560");

    BOOST_CHECK_EQUAL(tx.blobVersionedHashes.size(), 6);
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[0]),
        "01f8198b33db3461035e1621dd12498e57cf26efe9578b39054fbe5efdf83032");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[1]),
        "0152295a881b358db5dcf58b54661ee60f595de7f57eb93030a5d9e57bcae30e");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[2]),
        "014ea3a3d4fc547ccb6974c5c4deb7778b755b0b3d56be88c54ef3a39d209b4c");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[3]),
        "01b378a4a2a4a3806740ec38b5672d213c78bbcae34550d014a265fc262fe06e");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[4]),
        "01b83eca80127748b71bcaa6a8c9edbfd5a9fb47933032891c27e07668f48867");
    BOOST_CHECK_EQUAL(toHex(tx.blobVersionedHashes[5]),
        "01904e6186ecd84f6897659777846d5510bfbeb2863a93d8432f0fcf89c3e2c9");

    BOOST_CHECK_EQUAL(tx.accessList.size(), 3);
    BOOST_CHECK_EQUAL(
        tx.accessList[0].account.hexPrefixed(), "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6");
    BOOST_CHECK_EQUAL(tx.accessList[0].storageKeys.size(), 6);
    BOOST_CHECK_EQUAL(toHex(tx.accessList[0].storageKeys[5]),
        "a10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660");
    BOOST_CHECK_EQUAL(
        tx.accessList[1].account.hexPrefixed(), "0x8315177ab297ba92a06054ce80a67ed4dbd7ed3a");
    BOOST_CHECK_EQUAL(tx.accessList[1].storageKeys.size(), 9);
    BOOST_CHECK_EQUAL(
        tx.accessList[2].account.hexPrefixed(), "0xe64a54e2533fd126c2e452c5fab544d80e2e4eb5");
    BOOST_CHECK_EQUAL(tx.accessList[2].storageKeys.size(), 4);

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    auto rawTx2 = toHexStringWithPrefix(encoded);
    BOOST_CHECK_EQUAL(rawTx, rawTx2);

    auto encodeForSign = tx.encodeForSign();
    bcos::bytes sign{};
    sign.insert(sign.end(), tx.signatureR.begin(), tx.signatureR.end());
    sign.insert(sign.end(), tx.signatureS.begin(), tx.signatureS.end());
    sign.push_back(tx.signatureV);
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto [re, addr] = signatureImpl->recoverAddress(*hashImpl, hash, ref(sign));
    BOOST_CHECK(re);
    auto address = toHexStringWithPrefix(addr);
    BOOST_CHECK_EQUAL(address, "0xc1b634853cb333d3ad8663715b08f41a3aec47cc");
}

// ===== Tests for refactored decodeTransaction and Web3TransactionType =====

BOOST_AUTO_TEST_CASE(testWeb3TransactionTypeFromFramework)
{
    // Verify the new bcos::protocol::Web3TransactionType enum is accessible
    using bcos::protocol::Web3TransactionType;
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(Web3TransactionType::Legacy), 0);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(Web3TransactionType::EIP2930), 1);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(Web3TransactionType::EIP1559), 2);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(Web3TransactionType::EIP4844), 3);

    // Verify the alias in rpc namespace still works
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(rpc::TransactionType::Legacy),
        static_cast<uint8_t>(Web3TransactionType::Legacy));
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(rpc::TransactionType::EIP2930),
        static_cast<uint8_t>(Web3TransactionType::EIP2930));
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(rpc::TransactionType::EIP1559),
        static_cast<uint8_t>(Web3TransactionType::EIP1559));
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(rpc::TransactionType::EIP4844),
        static_cast<uint8_t>(Web3TransactionType::EIP4844));

    // Verify comparison operators work
    BOOST_CHECK(Web3TransactionType::Legacy < Web3TransactionType::EIP2930);
    BOOST_CHECK(Web3TransactionType::EIP2930 < Web3TransactionType::EIP1559);
    BOOST_CHECK(Web3TransactionType::EIP1559 < Web3TransactionType::EIP4844);
    BOOST_CHECK(Web3TransactionType::EIP4844 == Web3TransactionType::EIP4844);
}

BOOST_AUTO_TEST_CASE(testDecodeEmptyInput)
{
    bcos::bytes emptyBytes;
    auto bRef = bcos::ref(emptyBytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::InputTooShort);
}

BOOST_AUTO_TEST_CASE(testDecodeUnsupportedType)
{
    // 0x04 is not a valid EIP-2718 type
    bcos::bytes badBytes = {0x04, 0xc0};
    auto bRef = bcos::ref(badBytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::UnsupportedTransactionType);
}

BOOST_AUTO_TEST_CASE(testLegacyEncodeDecodeRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::Legacy;
    tx.nonce = 42;
    tx.maxFeePerGas = 20000000000;
    tx.maxPriorityFeePerGas = 20000000000;
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    tx.value = 1000000000000000000ull;
    tx.data = fromHex("a9059cbb");
    tx.chainId = 1;
    tx.signatureV = 1;
    tx.signatureR =
        HashType("0xbe67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717").asBytes();
    tx.signatureS =
        HashType("0x2d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718").asBytes();

    // Encode
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK(!encoded.empty());

    // Decode
    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::Legacy);
    BOOST_CHECK_EQUAL(decoded.nonce, 42);
    BOOST_CHECK_EQUAL(decoded.maxFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(decoded.gasLimit, 21000);
    BOOST_CHECK_EQUAL(decoded.to.value(), Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a"));
    BOOST_CHECK_EQUAL(decoded.value, 1000000000000000000ull);
}

BOOST_AUTO_TEST_CASE(testEIP2930EncodeDecodeRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP2930;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 30000000000;
    tx.gasLimit = 5748100;
    tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    tx.value = 2000000000000000000ull;
    tx.data = fromHex("6ebaf477f83e051589c1188bcc6ddccd");
    tx.accessList = s_accessList;
    tx.signatureV = 0;
    tx.signatureR =
        HashType("0x36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0").asBytes();
    tx.signatureS =
        HashType("0x5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094").asBytes();

    // Encode
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK(!encoded.empty());
    BOOST_CHECK_EQUAL(encoded[0], 0x01);  // EIP-2930 type byte

    // Decode
    Web3Transaction decoded{};
    auto bRef = bcos::ref(encoded);
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::EIP2930);
    BOOST_CHECK_EQUAL(decoded.chainId.value(), 5);
    BOOST_CHECK_EQUAL(decoded.nonce, 7);
    BOOST_CHECK_EQUAL(decoded.maxFeePerGas, 30000000000);
    BOOST_CHECK_EQUAL(decoded.maxPriorityFeePerGas, 30000000000);
    BOOST_CHECK(decoded.accessList == s_accessList);
}

BOOST_AUTO_TEST_CASE(testEIP1559EncodeDecodeRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 10000000000;
    tx.gasLimit = 5748100;
    tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    tx.value = 2000000000000000000ull;
    tx.data = fromHex("6ebaf477f83e051589c1188bcc6ddccd");
    tx.accessList = s_accessList;
    tx.signatureV = 0;
    tx.signatureR =
        HashType("0x36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0").asBytes();
    tx.signatureS =
        HashType("0x5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094").asBytes();

    // Encode
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK(!encoded.empty());
    BOOST_CHECK_EQUAL(encoded[0], 0x02);  // EIP-1559 type byte

    // Decode
    Web3Transaction decoded{};
    auto bRef = bcos::ref(encoded);
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK_EQUAL(decoded.chainId.value(), 5);
    BOOST_CHECK_EQUAL(decoded.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(decoded.maxFeePerGas, 30000000000);
}

BOOST_AUTO_TEST_CASE(testEIP4844EncodeDecodeRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP4844;
    tx.chainId = 5;
    tx.nonce = 7;
    tx.maxFeePerGas = 30000000000;
    tx.maxPriorityFeePerGas = 10000000000;
    tx.gasLimit = 5748100;
    tx.to = Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    tx.value = 0;
    tx.data = fromHex("04f7");
    tx.accessList = s_accessList;
    tx.maxFeePerBlobGas = 123;
    tx.blobVersionedHashes = {
        HashType("0xc6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210"),
        HashType("0x8aaeccaf3873d07cef005aca28c39f8a9f8bdb1ec8d79ffc25afc0a4fa2ab736"),
    };
    tx.signatureV = 1;
    tx.signatureR =
        HashType("0x36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0").asBytes();
    tx.signatureS =
        HashType("0x5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094").asBytes();

    // Encode
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK(!encoded.empty());
    BOOST_CHECK_EQUAL(encoded[0], 0x03);  // EIP-4844 type byte

    // Decode
    Web3Transaction decoded{};
    auto bRef = bcos::ref(encoded);
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::EIP4844);
    BOOST_CHECK_EQUAL(decoded.maxFeePerBlobGas, 123);
    BOOST_CHECK_EQUAL(decoded.blobVersionedHashes.size(), 2);
    BOOST_CHECK(decoded.accessList == s_accessList);
}

BOOST_AUTO_TEST_CASE(testLegacyWithoutSignature)
{
    // decodeFromPayload expects encodeForSign() output, not a signed raw tx
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000afa9"
        "059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1d78b58c40"
        "0026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690516512020171c"
        "1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);

    auto signPayload = tx.encodeForSign();
    auto signRef = bcos::ref(signPayload);
    Web3Transaction payload{};
    e = codec::rlp::decodeFromPayload(signRef, payload);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(payload.type == rpc::TransactionType::Legacy);
    BOOST_CHECK_EQUAL(payload.nonce, 12);
    BOOST_CHECK_EQUAL(payload.chainId.value(), 1);
    BOOST_CHECK(payload.signatureR.empty());
    BOOST_CHECK(payload.signatureS.empty());
}

BOOST_AUTO_TEST_CASE(testEIP1559WithoutSignature)
{
    constexpr std::string_view rawTx =
        "0x02f8f805078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881b"
        "c16d674ec80000906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4"
        "cb697baef842a00000000000000000000000000000000000000000000000000000000000000003a00000000000"
        "000000000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb"
        "8c189413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4"
        "741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);

    auto signPayload = tx.encodeForSign();
    auto signRef = bcos::ref(signPayload);
    Web3Transaction payload{};
    e = codec::rlp::decodeFromPayload(signRef, payload);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(payload.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK_EQUAL(payload.maxPriorityFeePerGas, 10000000000);
    BOOST_CHECK_EQUAL(payload.maxFeePerGas, 30000000000);
    BOOST_CHECK(payload.signatureR.empty());
    BOOST_CHECK(payload.signatureS.empty());
}

BOOST_AUTO_TEST_CASE(testEncodeForSignConsistency)
{
    // Verify encodeForSign roundtrips through decodeFromPayload
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 1;
    tx.nonce = 100;
    tx.maxFeePerGas = 50000000000;
    tx.maxPriorityFeePerGas = 2000000000;
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    tx.value = 1000000000000000000ull;

    auto signData = tx.encodeForSign();
    BOOST_CHECK(!signData.empty());
    BOOST_CHECK_EQUAL(signData[0], 0x02);

    auto signRef = bcos::ref(signData);
    Web3Transaction decoded{};
    auto e = codec::rlp::decodeFromPayload(signRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::EIP1559);
    BOOST_CHECK_EQUAL(decoded.chainId.value(), 1);
    BOOST_CHECK_EQUAL(decoded.nonce, 100);
    BOOST_CHECK_EQUAL(decoded.maxFeePerGas, tx.maxFeePerGas);
    BOOST_CHECK_EQUAL(decoded.maxPriorityFeePerGas, tx.maxPriorityFeePerGas);
    BOOST_CHECK_EQUAL(decoded.gasLimit, 21000);
    BOOST_CHECK(decoded.signatureR.empty());
    BOOST_CHECK(decoded.signatureS.empty());
}

BOOST_AUTO_TEST_CASE(testDecodeTrailingBytesRejected)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 1;
    tx.nonce = 1;
    tx.maxFeePerGas = 1000000000;
    tx.maxPriorityFeePerGas = 1000000000;
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    tx.signatureV = 0;
    tx.signatureR =
        HashType("0x36b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0").asBytes();
    tx.signatureS =
        HashType("0x5edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094").asBytes();

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    encoded.push_back(0x00);

    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::UnexpectedListElements);
}

BOOST_AUTO_TEST_CASE(testTxHashConsistency)
{
    // Verify txHash is consistent across encode-decode-encode cycle
    constexpr std::string_view rawTx =
        "0x02f8720183015b148085089a36ae8682520894e10f39a0dfb9e380b6d176eb7183af32b68028d78806e9ba3b"
        "d88b600080c080a032ab966d1c9cc2be6952713a1599a95a14f6e92c9f62d7fa40aa62d8b764ffcfa060bdbe7b"
        "8e66a0c681a90d4da0c7c0a4ba9321d49fc5c65bfddb847539e35d56";
    auto bytes = fromHexWithPrefix(rawTx);
    auto originalHash = bcos::crypto::keccak256Hash(bcos::ref(bytes)).hexPrefixed();

    // Decode
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);

    // txHash should match keccak256 of original bytes
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(), originalHash);

    // Re-encode and hash should still match
    bcos::bytes reEncoded{};
    codec::rlp::encode(reEncoded, tx);
    BOOST_CHECK_EQUAL(
        bcos::crypto::keccak256Hash(bcos::ref(reEncoded)).hexPrefixed(), originalHash);
}

// ===== Additional RLP hardening and edge-case tests =====

BOOST_AUTO_TEST_CASE(testPreEIP155LegacyVRecovery)
{
    // v=27 (pre-EIP-155): chainId unset, yParity stored as signatureV=0
    constexpr std::string_view rawTx =
        "0xf8ac82017c8504a817c800835fefd89409d07ecb4d6f32e91503c04b192e3bdeb7f857f480b8442c7128d"
        "700000000000000000000000000000000000000000000000009bc24e89949a000000000000000000000000"
        "000000000000000000000000000000000001ba0cd372eb41b6b4e9e576233bb29c1492e0329fac1331f492"
        "a69e4a1b586a1a28ba032950cc4184ca8b0d45b24d13345157b4153d7ccc0d187dbab018be07726d186";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.type == rpc::TransactionType::Legacy);
    BOOST_CHECK(!tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.signatureV, 0);
    BOOST_CHECK_EQUAL(tx.getSignatureV(), 27);
}

BOOST_AUTO_TEST_CASE(testLegacyEIP155VRecovery)
{
    // v=38 on chainId=1 => yParity=1, chainId recovered as 1
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000a"
        "fa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1"
        "d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690"
        "516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = fromHexWithPrefix(rawTx);
    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(tx.chainId.has_value());
    BOOST_CHECK_EQUAL(tx.chainId.value(), 1);
    BOOST_CHECK_EQUAL(tx.signatureV, 1);
    BOOST_CHECK_EQUAL(tx.getSignatureV(), 38);
}

BOOST_AUTO_TEST_CASE(testLegacyInvalidSignatureV)
{
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000a"
        "fa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1"
        "d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690"
        "516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = fromHexWithPrefix(rawTx);
    // v=38 (0x26) -> invalid v=34 (0x22), which is in (28, 35)
    auto hex = toHex(bytes);
    auto pos = hex.find("26a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717");
    BOOST_REQUIRE(pos != std::string::npos);
    hex.replace(pos, 2, "22");
    bytes = fromHex(hex);

    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::InvalidVInSignature);
}

BOOST_AUTO_TEST_CASE(testLegacyContractCreationRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::Legacy;
    tx.nonce = 9;
    tx.maxFeePerGas = 300;
    tx.maxPriorityFeePerGas = 300;
    tx.gasLimit = 22000000;
    tx.to = std::nullopt;
    tx.value = 0;
    tx.data = fromHex(
        "6080604052348015600e575f5ffd5b506040517fcf16a92280c1bbb43f72d31126b724d508df2877835849e874"
        "4017ab36a9b47f905f90a1");
    tx.chainId = 20200;
    tx.signatureV = 0;
    tx.signatureR =
        HashType("0xf0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693").asBytes();
    tx.signatureS =
        HashType("0x53ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd").asBytes();

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK(!encoded.empty());

    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(!decoded.to.has_value());
    BOOST_CHECK_EQUAL(decoded.nonce, 9);
    BOOST_CHECK_EQUAL(decoded.chainId.value(), 20200);
    BOOST_CHECK_EQUAL(toHex(decoded.data), toHex(tx.data));

    bcos::bytes reEncoded{};
    codec::rlp::encode(reEncoded, decoded);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(encoded), toHexStringWithPrefix(reEncoded));
}

BOOST_AUTO_TEST_CASE(testEIP1559ContractCreationRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 20200;
    tx.nonce = 9;
    tx.maxPriorityFeePerGas = 10;
    tx.maxFeePerGas = 300;
    tx.gasLimit = 22000000;
    tx.to = std::nullopt;
    tx.value = 0;
    tx.data = fromHex("6080604052348015600e575f5ffd5b50");
    tx.signatureV = 0;
    tx.signatureR =
        HashType("0xf0643ec9f740e363dfa8d0f902a643dd4828a04cd80dd733822f2dd636fb6693").asBytes();
    tx.signatureS =
        HashType("0x53ad48b39f2bbb3b3a5e073074bcb1dd43d908b292ad9078d4478dc42f1195bd").asBytes();

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    BOOST_CHECK_EQUAL(encoded[0], 0x02);

    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(!decoded.to.has_value());
    BOOST_CHECK_EQUAL(decoded.type, rpc::TransactionType::EIP1559);
}

BOOST_AUTO_TEST_CASE(testDecodeTruncatedInput)
{
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000a"
        "fa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1"
        "d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690"
        "516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = fromHexWithPrefix(rawTx);
    bytes.resize(bytes.size() - 10);

    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::InputTooShort);
}

BOOST_AUTO_TEST_CASE(testDecodeExtraListElementsRejected)
{
    constexpr std::string_view rawTx =
        "0xf89b0c8504a817c80082520894727fc6a68321b754475c668a6abfb6e9e71c169a888ac7230489e80000a"
        "fa9059cbb000000000213ed0f886efd100b67c7e4ec0a85a7d20dc971600000000000000000000015af1"
        "d78b58c400026a0be67e0a07db67da8d446f76add590e54b6e92cb6b8f9835aeb67540579a27717a02d690"
        "516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718";
    auto bytes = injectExtraListElement(fromHexWithPrefix(rawTx));

    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::UnexpectedListElements);
}

BOOST_AUTO_TEST_CASE(testEIP2930TxHashConsistency)
{
    constexpr std::string_view rawTx =
        "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec800"
        "00906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842"
        "a000000000000000000000000000000000000000000000000000000000000000003a0000000000000000000000"
        "000000000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189"
        "413c080a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741"
        "c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto originalHash = bcos::crypto::keccak256Hash(bcos::ref(bytes)).hexPrefixed();

    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(), originalHash);

    bcos::bytes reEncoded{};
    codec::rlp::encode(reEncoded, tx);
    BOOST_CHECK_EQUAL(rawTx, toHexStringWithPrefix(reEncoded));
}

BOOST_AUTO_TEST_CASE(testEIP4844TxHashConsistency)
{
    constexpr std::string_view rawTx =
        "0x03f9012705078502540be4008506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d780"
        "8204f7f872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a000000000000000000000000000"
        "000000000000000000000000000000000003a000000000000000000000000000000000000000000000000000"
        "0000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c07bf842a0c6bdd1de713471bd6c"
        "fa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210a08aaeccaf3873d07cef005aca28c39f8a9f8bdb1"
        "ec8d79ffc25afc0a4fa2ab73601a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16"
        "b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a62787b4510465fadbfe39ee4094";
    auto bytes = fromHexWithPrefix(rawTx);
    auto originalHash = bcos::crypto::keccak256Hash(bcos::ref(bytes)).hexPrefixed();

    auto bRef = bcos::ref(bytes);
    Web3Transaction tx{};
    auto e = codec::rlp::decode(bRef, tx);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK_EQUAL(tx.txHash().hexPrefixed(), originalHash);

    bcos::bytes reEncoded{};
    codec::rlp::encode(reEncoded, tx);
    BOOST_CHECK_EQUAL(rawTx, toHexStringWithPrefix(reEncoded));
}

BOOST_AUTO_TEST_CASE(testDecodeOverlongSignatureRejected)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::Legacy;
    tx.nonce = 1;
    tx.maxFeePerGas = 1000000000;
    tx.maxPriorityFeePerGas = 1000000000;
    tx.gasLimit = 21000;
    tx.to = Address("0x727fc6a68321b754475c668a6abfb6e9e71c169a");
    tx.chainId = 1;
    tx.signatureV = 0;
    tx.signatureR.assign(crypto::SECP256K1_SIGNATURE_R_LEN + 1, 0xab);
    tx.signatureS =
        HashType("0x2d690516512020171c1ec870f6ff45398cc8609250326be89915fb538e7bd718").asBytes();

    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);

    auto bRef = bcos::ref(encoded);
    Web3Transaction decoded{};
    auto e = codec::rlp::decode(bRef, decoded);
    BOOST_CHECK(e != nullptr);
    BOOST_CHECK_EQUAL(e->errorCode(), bcos::codec::rlp::InputTooLong);
}

BOOST_AUTO_TEST_CASE(testLegacyEncodeForSignRoundtrip)
{
    Web3Transaction tx{};
    tx.type = rpc::TransactionType::Legacy;
    tx.nonce = 19;
    tx.maxFeePerGas = 20000000000;
    tx.maxPriorityFeePerGas = 20000000000;
    tx.gasLimit = 210000;
    tx.to = Address("0x1e58529dAA467406645d0f4B63dec96CA0b87d70");
    tx.value = 1000000000000000000ull;
    tx.data = fromHex("deadbeef");
    tx.chainId = 31337;

    auto signPayload = tx.encodeForSign();
    auto signRef = bcos::ref(signPayload);
    Web3Transaction decoded{};
    auto e = codec::rlp::decodeFromPayload(signRef, decoded);
    BOOST_CHECK(e == nullptr);
    BOOST_CHECK(decoded.type == rpc::TransactionType::Legacy);
    BOOST_CHECK_EQUAL(decoded.nonce, 19);
    BOOST_CHECK_EQUAL(decoded.chainId.value(), 31337);
    BOOST_CHECK_EQUAL(decoded.maxFeePerGas, 20000000000);
    BOOST_CHECK_EQUAL(decoded.gasLimit, 210000);
    BOOST_CHECK_EQUAL(decoded.to.value(), tx.to.value());
    BOOST_CHECK_EQUAL(decoded.value, tx.value);
    BOOST_CHECK_EQUAL(toHex(decoded.data), toHex(tx.data));
}

// ===== Unified encode/decode roundtrip (positive + negative) =====

BOOST_AUTO_TEST_CASE(unifiedRoundtripLegacyEIP155)
{
    assertEncodeDecodeRoundtrip(makeLegacySignedTx(true, false));
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripLegacyPreEIP155)
{
    assertEncodeDecodeRoundtrip(makeLegacySignedTx(false, false));
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripLegacyContractCreation)
{
    assertEncodeDecodeRoundtrip(makeLegacySignedTx(true, true));
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripEIP2930)
{
    assertEncodeDecodeRoundtrip(makeEIP2930SignedTx());
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripEIP1559)
{
    assertEncodeDecodeRoundtrip(makeEIP1559SignedTx(false));
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripEIP1559ContractCreation)
{
    assertEncodeDecodeRoundtrip(makeEIP1559SignedTx(true));
}

BOOST_AUTO_TEST_CASE(unifiedRoundtripEIP4844)
{
    assertEncodeDecodeRoundtrip(makeEIP4844SignedTx());
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsEmptyInput)
{
    assertDecodeFails({}, DecodingError::InputTooShort);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsUnsupportedType)
{
    assertDecodeFails({0x04, 0xc0}, DecodingError::UnsupportedTransactionType);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsTruncatedLegacy)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeLegacySignedTx(true, false));
    encoded.resize(encoded.size() - 10);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooShort);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsTruncatedEIP1559)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeEIP1559SignedTx(false));
    encoded.resize(encoded.size() - 10);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooShort);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsTruncatedEIP2930)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeEIP2930SignedTx());
    encoded.resize(encoded.size() - 10);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooShort);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsTruncatedEIP4844)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeEIP4844SignedTx());
    encoded.resize(encoded.size() - 10);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooShort);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsExtraListElementLegacy)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeLegacySignedTx(true, false));
    assertDecodeFails(
        injectExtraListElement(std::move(encoded)), DecodingError::UnexpectedListElements);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsTrailingBytes)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeEIP1559SignedTx(false));
    encoded.push_back(0x00);
    assertDecodeFails(std::move(encoded), DecodingError::UnexpectedListElements);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsInvalidLegacyV)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeLegacySignedTx(true, false));
    auto hex = toHex(encoded);
    auto const marker = "26a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0";
    auto pos = hex.find(marker);
    BOOST_REQUIRE(pos != std::string::npos);
    hex.replace(pos, 2, "22");
    assertDecodeFails(fromHex(hex), DecodingError::InvalidVInSignature);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsOverlongSignatureR)
{
    auto tx = makeLegacySignedTx(true, false);
    tx.signatureR.assign(crypto::SECP256K1_SIGNATURE_R_LEN + 1, 0xab);
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooLong);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFailsOverlongSignatureS)
{
    auto tx = makeEIP1559SignedTx(false);
    tx.signatureS.assign(crypto::SECP256K1_SIGNATURE_S_LEN + 1, 0xcd);
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, tx);
    assertDecodeFails(std::move(encoded), DecodingError::InputTooLong);
}

BOOST_AUTO_TEST_CASE(unifiedDecodeFromPayloadFailsOnSignedLegacyTx)
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, makeLegacySignedTx(true, false));
    auto bRef = bcos::ref(encoded);
    Web3Transaction tx{};
    auto e = codec::rlp::decodeFromPayload(bRef, tx);
    BOOST_CHECK(e != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test