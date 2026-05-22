/**
 * @file Web3Eip7702ApplyTest.cpp
 * @brief Unit tests for EIP-7702 authorization tuple secp256k1 recovery (apply path).
 */

#include "../src/Web3Eip7702Apply.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstring>

using namespace bcos;
using namespace bcos::executor;

namespace
{
crypto::Secp256k1KeyPair testKeyPair()
{
    return crypto::Secp256k1KeyPair(std::make_shared<crypto::KeyImpl>(
        fromHex("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")));
}

Eip7702Authorization signTuple(crypto::Hash::Ptr const& hashImpl,
    crypto::KeyPairInterface const& keyPair, uint64_t chainId, Address const& target,
    uint64_t nonce)
{
    Eip7702Authorization auth;
    auth.chainId = chainId;
    auth.address = target;
    auth.nonce = nonce;

    bytes rlpList;
    codec::rlp::encode(rlpList, chainId, target, nonce);
    bytes signDomain;
    signDomain.reserve(1 + rlpList.size());
    signDomain.push_back(0x05);
    signDomain.insert(signDomain.end(), rlpList.begin(), rlpList.end());
    auto const hash = hashImpl->hash(bytesConstRef(signDomain.data(), signDomain.size()));

    crypto::Secp256k1Crypto secp;
    auto const signature = secp.sign(keyPair, hash, false);
    std::memcpy(auth.r.data(), signature->data(), sizeof(auth.r));
    std::memcpy(
        auth.s.data(), signature->data() + crypto::SECP256K1_SIGNATURE_R_LEN, sizeof(auth.s));
    auth.yParity = signature->back();
    return auth;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3Eip7702Apply)

BOOST_AUTO_TEST_CASE(recover_valid_tuple_matches_authority)
{
    auto const hashImpl = std::make_shared<crypto::Keccak256>();
    auto const keyPair = testKeyPair();
    Address const target("0x2222222222222222222222222222222222222222");
    auto const auth = signTuple(hashImpl, keyPair, 1, target, 0);

    auto const recovered = recoverEip7702Authority(hashImpl, auth);
    BOOST_REQUIRE(recovered);
    BOOST_CHECK_EQUAL(*recovered, crypto::calculateAddress(hashImpl, keyPair.publicKey()));
}

BOOST_AUTO_TEST_CASE(recover_invalid_y_parity_returns_nullopt)
{
    auto const hashImpl = std::make_shared<crypto::Keccak256>();
    auto const keyPair = testKeyPair();
    Address const target("0x2222222222222222222222222222222222222222");
    auto auth = signTuple(hashImpl, keyPair, 1, target, 0);
    auth.yParity = 9;

    BOOST_CHECK(!recoverEip7702Authority(hashImpl, auth));
}

BOOST_AUTO_TEST_SUITE_END()
