#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/Features.h>

namespace bcos::executor
{

inline bool isEip7702Enabled(
    ledger::Features const& features, int executorVersion, crypto::HashImplType hashType) noexcept
{
    if (!features.get(ledger::Features::Flag::feature_evm_prague))
    {
        return false;
    }
    if (executorVersion != 1)
    {
        return false;
    }
    if (hashType == crypto::HashImplType::Sm3Hash)
    {
        return false;
    }
    return true;
}

inline bool isEip7702Enabled(ledger::Features const& features, int executorVersion,
    crypto::Hash::Ptr const& hashImpl) noexcept
{
    if (!hashImpl)
    {
        return false;
    }
    return isEip7702Enabled(features, executorVersion, hashImpl->getHashImplType());
}

inline bool isEip7702Enabled(ledger::Features const& features, int executorVersion,
    crypto::SignatureCrypto const& signatureImpl) noexcept
{
    if (!features.get(ledger::Features::Flag::feature_evm_prague))
    {
        return false;
    }
    if (executorVersion != 1)
    {
        return false;
    }
    if (dynamic_cast<crypto::Secp256k1Crypto const*>(&signatureImpl) == nullptr)
    {
        return false;
    }
    return true;
}

}  // namespace bcos::executor
