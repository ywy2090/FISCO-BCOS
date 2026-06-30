#pragma once

#include "bcos-evm/eth/eip/Eip7702.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <cstring>
#include <memory>

namespace bcos::evm::test
{

inline evmc_address evmcAddressFromKeyPair(
    bcos::crypto::KeyPairInterface const& keyPair, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const addr = bcos::crypto::calculateAddress(hashImpl, keyPair.publicKey());
    evmc_address out{};
    std::memcpy(out.bytes, addr.data(), sizeof(out.bytes));
    return out;
}

inline SetCodeAuthorization signSetCodeAuthorization(bcos::u256 chainId,
    evmc_address delegateTarget, uint64_t nonce, bcos::crypto::KeyPairInterface const& keyPair)
{
    bcos::FixedBytes<20> targetFixed{
        bcos::bytesConstRef{delegateTarget.bytes, sizeof(delegateTarget.bytes)}};
    bcos::bytes encodedPayload;
    bcos::codec::rlp::encode(encodedPayload, chainId, targetFixed, nonce);

    bcos::bytes signPayload;
    signPayload.push_back(0x05);
    signPayload.insert(signPayload.end(), encodedPayload.begin(), encodedPayload.end());

    bcos::crypto::Keccak256 hashImpl;
    auto const hash = bcos::crypto::keccak256Hash(bcos::ref(signPayload));

    bcos::crypto::Secp256k1Crypto signatureImpl;
    auto signature = signatureImpl.sign(keyPair, hash, false);

    SetCodeAuthorization auth;
    auth.chainId = chainId;
    auth.address = delegateTarget;
    auth.nonce = nonce;
    auth.yParity = signature->at(64);
    auth.signatureR = bcos::bytes(signature->begin(), signature->begin() + 32);
    auth.signatureS = bcos::bytes(signature->begin() + 32, signature->begin() + 64);
    return auth;
}

struct TestAuthKeyPair
{
    std::shared_ptr<bcos::crypto::KeyPairInterface> keyPair;
    bcos::crypto::Hash::Ptr hash = std::make_shared<bcos::crypto::Keccak256>();

    static TestAuthKeyPair generate()
    {
        bcos::crypto::Secp256k1Crypto crypto;
        TestAuthKeyPair out;
        out.keyPair = crypto.generateKeyPair();
        return out;
    }

    evmc_address address() const { return evmcAddressFromKeyPair(*keyPair, hash); }

    SetCodeAuthorization sign(
        evmc_address delegateTarget, uint64_t nonce, bcos::u256 chainId = bcos::u256(1)) const
    {
        return signSetCodeAuthorization(chainId, delegateTarget, nonce, *keyPair);
    }
};

}  // namespace bcos::evm::test
