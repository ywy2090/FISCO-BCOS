/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file Web3Transaction.cpp
 * @author: kyonGuo
 * @date 2024/4/8
 */

#include "Web3Transaction.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-utilities/BoostLog.h>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/move.hpp>
#include <utility>

namespace bcos
{
namespace rpc
{
using codec::rlp::decode;
using codec::rlp::encode;
using codec::rlp::header;
using codec::rlp::length;

static bytesConstRef getSignatureRef(bytesConstRef input)
{
    const auto* it = ::ranges::find_if(input, [](byte b) { return b != 0; });
    return {it, input.size() - (it - input.begin())};
}

bcos::bytes Web3Transaction::encodeForSign() const
{
    bcos::bytes out;
    if (type == TransactionType::Legacy)
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0])
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, nonce);
        codec::rlp::encode(out, maxFeePerGas);  // gasPrice for legacy
        codec::rlp::encode(out, gasLimit);
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        if (chainId)
        {
            // EIP-155
            codec::rlp::encode(out, chainId.value());
            codec::rlp::encode(out, 0U);
            codec::rlp::encode(out, 0U);
        }
    }
    else if (type == TransactionType::EIP2930)
    {
        // 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList])
        out.push_back(static_cast<byte>(type));
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, chainId.value_or(0));
        codec::rlp::encode(out, nonce);
        codec::rlp::encode(out, maxFeePerGas);  // gasPrice for EIP2930
        codec::rlp::encode(out, gasLimit);
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        codec::rlp::encode(out, accessList);
    }
    else if (type == TransactionType::EIP1559)
    {
        // 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
        //               gasLimit, to, value, data, accessList])
        out.push_back(static_cast<byte>(type));
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, chainId.value_or(0));
        codec::rlp::encode(out, nonce);
        codec::rlp::encode(out, maxPriorityFeePerGas);
        codec::rlp::encode(out, maxFeePerGas);
        codec::rlp::encode(out, gasLimit);
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        codec::rlp::encode(out, accessList);
    }
    else if (type == TransactionType::EIP4844)
    {
        // 0x03 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
        //               gasLimit, to, value, data, accessList, maxFeePerBlobGas,
        //               blobVersionedHashes])
        out.push_back(static_cast<byte>(type));
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, chainId.value_or(0));
        codec::rlp::encode(out, nonce);
        codec::rlp::encode(out, maxPriorityFeePerGas);
        codec::rlp::encode(out, maxFeePerGas);
        codec::rlp::encode(out, gasLimit);
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        codec::rlp::encode(out, accessList);
        codec::rlp::encode(out, maxFeePerBlobGas);
        codec::rlp::encode(out, blobVersionedHashes);
    }
    else
    {
        BCOS_LOG(WARNING) << LOG_BADGE("Web3Transaction")
                          << LOG_DESC("encodeForSign unsupported type")
                          << LOG_KV("type", static_cast<uint16_t>(type));
    }
    return out;
}

bcos::crypto::HashType Web3Transaction::txHash() const
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, *this);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::crypto::HashType Web3Transaction::hashForSign() const
{
    auto encodeForSign = this->encodeForSign();
    return bcos::crypto::keccak256Hash(bcos::ref(encodeForSign));
}

bcostars::Transaction Web3Transaction::takeToTarsTransaction()
{
    bcostars::Transaction tarsTx{};
    tarsTx.data.to = (this->to.has_value()) ? this->to.value().hexPrefixed() : "";
    tarsTx.data.input.reserve(this->data.size());
    ::ranges::move(this->data, std::back_inserter(tarsTx.data.input));

    tarsTx.data.value = "0x" + this->value.str(0, std::ios_base::hex);
    tarsTx.data.gasLimit = this->gasLimit;
    if (static_cast<uint8_t>(this->type) >= static_cast<uint8_t>(TransactionType::EIP1559))
    {
        tarsTx.data.maxFeePerGas = "0x" + this->maxFeePerGas.str(0, std::ios_base::hex);
        tarsTx.data.maxPriorityFeePerGas =
            "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    else
    {
        tarsTx.data.gasPrice = "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tarsTx.web3TypedTxKind = static_cast<tars::Char>(static_cast<uint8_t>(this->type));
    if (!this->accessList.empty())
    {
        tarsTx.data.accessList.reserve(this->accessList.size());
        for (auto const& entry : this->accessList)
        {
            bcostars::Web3AccessListEntry tarsEntry;
            tarsEntry.account = entry.account.hex();
            for (auto const& key : entry.storageKeys)
            {
                tarsEntry.storageKeys.emplace_back(key.begin(), key.end());
            }
            tarsTx.data.accessList.emplace_back(std::move(tarsEntry));
        }
    }

    // Only call encodeForSign() once, store in extraTransactionBytes for TxValidator::verify()
    auto encodedForSign = this->encodeForSign();
    tarsTx.extraTransactionBytes.reserve(encodedForSign.size());
    ::ranges::move(encodedForSign, std::back_inserter(tarsTx.extraTransactionBytes));

    // FISCO BCOS signature is r||s||v
    tarsTx.signature.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    ::ranges::move(this->signatureR, std::back_inserter(tarsTx.signature));
    ::ranges::move(this->signatureS, std::back_inserter(tarsTx.signature));
    tarsTx.signature.push_back(static_cast<tars::Char>(this->signatureV));

    tarsTx.data.nonce = toQuantity(this->nonce);
    tarsTx.data.chainID = std::to_string(this->chainId.value_or(0));

    // dataHash and sender left empty — TxValidator::verify() computes them
    return tarsTx;
}
std::ostream& operator<<(std::ostream& _out, const TransactionType& _in)
{
    _out << magic_enum::enum_name(_in);
    return _out;
}
uint64_t Web3Transaction::getSignatureV() const
{
    // EIP-155: Simple replay attack protection
    if (chainId.has_value())
    {
        return chainId.value() * 2 + 35 + signatureV;
    }
    return signatureV + 27;
}
std::string Web3Transaction::sender() const
{
    bcos::bytes sign{};
    sign.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    sign.insert(sign.end(), signatureR.begin(), signatureR.end());
    sign.insert(sign.end(), signatureS.begin(), signatureS.end());
    sign.push_back(signatureV);
    bcos::crypto::Keccak256 hashImpl;
    auto encodeForSign = this->encodeForSign();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    const bcos::crypto::Secp256k1Crypto signatureImpl;
    auto [recovered, addr] = signatureImpl.recoverAddress(hashImpl, hash, ref(sign));
    if (!recovered)
    {
        return {};
    }
    return toHexStringWithPrefix(addr);
}
std::string Web3Transaction::toString() const noexcept
{
    std::stringstream stringstream{};
    stringstream << " chainId: " << this->chainId.value_or(0) << " hash:" << this->txHash().hex()
                 << " type: " << static_cast<uint16_t>(this->type)
                 << " to: " << this->to.value_or(Address()).hex() << " data: " << toHex(this->data)
                 << " value: " << this->value << " nonce: " << this->nonce
                 << " gasLimit: " << this->gasLimit
                 << " maxPriorityFeePerGas: " << this->maxPriorityFeePerGas
                 << " maxFeePerGas: " << this->maxFeePerGas
                 << " maxFeePerBlobGas: " << this->maxFeePerBlobGas
                 << " blobVersionedHashes: " << this->blobVersionedHashes
                 << " sender: " << this->sender() << " signatureR: " << toHex(this->signatureR)
                 << " signatureS: " << toHex(this->signatureS)
                 << " signatureV: " << this->signatureV;
    return stringstream.str();
}
}  // namespace rpc

namespace codec::rlp
{
using namespace bcos::rpc;
Header header(const AccessListEntry& entry) noexcept
{
    auto len = length(entry.storageKeys);
    return {.isList = true, .payloadLength = Address::SIZE + 1 + len};
}

size_t length(AccessListEntry const& entry) noexcept
{
    auto head = header(entry);
    return lengthOfLength(head.payloadLength) + head.payloadLength;
}
Header headerTxBase(const Web3Transaction& tx) noexcept
{
    Header h{.isList = true};

    if (tx.type == TransactionType::Legacy)
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data])
        h.payloadLength += length(tx.nonce);
        h.payloadLength += length(tx.maxFeePerGas);  // gasPrice for legacy
        h.payloadLength += length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += length(tx.value);
        h.payloadLength += length(tx.data);
    }
    else if (tx.type == TransactionType::EIP2930)
    {
        // rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList])
        h.payloadLength += length(tx.chainId.value_or(0));
        h.payloadLength += length(tx.nonce);
        h.payloadLength += length(tx.maxFeePerGas);  // gasPrice for EIP2930
        h.payloadLength += length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += length(tx.value);
        h.payloadLength += length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
    }
    else if (tx.type == TransactionType::EIP1559)
    {
        // rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data,
        //      accessList])
        h.payloadLength += length(tx.chainId.value_or(0));
        h.payloadLength += length(tx.nonce);
        h.payloadLength += length(tx.maxPriorityFeePerGas);
        h.payloadLength += length(tx.maxFeePerGas);
        h.payloadLength += length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += length(tx.value);
        h.payloadLength += length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
    }
    else if (tx.type == TransactionType::EIP4844)
    {
        // rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data,
        //      accessList, maxFeePerBlobGas, blobVersionedHashes])
        h.payloadLength += length(tx.chainId.value_or(0));
        h.payloadLength += length(tx.nonce);
        h.payloadLength += length(tx.maxPriorityFeePerGas);
        h.payloadLength += length(tx.maxFeePerGas);
        h.payloadLength += length(tx.gasLimit);
        h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
        h.payloadLength += length(tx.value);
        h.payloadLength += length(tx.data);
        h.payloadLength += codec::rlp::length(tx.accessList);
        h.payloadLength += length(tx.maxFeePerBlobGas);
        h.payloadLength += length(tx.blobVersionedHashes);
    }
    else
    {
        BCOS_LOG(WARNING) << LOG_BADGE("Web3Transaction")
                          << LOG_DESC("headerTxBase unsupported type")
                          << LOG_KV("type", static_cast<uint16_t>(tx.type));
    }

    return h;
}
Header header(Web3Transaction const& tx) noexcept
{
    auto header = headerTxBase(tx);
    if (tx.type == TransactionType::Legacy)
    {
        header.payloadLength += length(tx.getSignatureV());
    }
    else
    {
        header.payloadLength += length(tx.signatureV);
    }
    header.payloadLength += length(getSignatureRef(ref(tx.signatureR)));
    header.payloadLength += length(getSignatureRef(ref(tx.signatureS)));
    return header;
}
Header headerForSign(Web3Transaction const& tx) noexcept
{
    auto header = headerTxBase(tx);
    if (tx.type == TransactionType::Legacy && tx.chainId)
    {
        header.payloadLength += length(tx.chainId.value()) + 2;
    }
    return header;
}
size_t length(Web3Transaction const& tx) noexcept
{
    auto head = header(tx);
    auto len = lengthOfLength(head.payloadLength) + head.payloadLength;
    len = (tx.type == TransactionType::Legacy) ? len : lengthOfLength(len + 1) + len + 1;
    return len;
}
void encode(bcos::bytes& out, const AccessListEntry& entry) noexcept
{
    encodeHeader(out, header(entry));
    encode(out, entry.account.ref());
    encode(out, entry.storageKeys);
}
void encode(bcos::bytes& out, const Web3Transaction& tx) noexcept
{
    if (tx.type == TransactionType::Legacy)
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
        encodeHeader(out, header(tx));
        encode(out, tx.nonce);
        encode(out, tx.maxFeePerGas);  // gasPrice for legacy
        encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.getSignatureV());
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
    }
    else if (tx.type == TransactionType::EIP2930)
    {
        // 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
        //              signatureYParity, signatureR, signatureS])
        out.push_back(static_cast<bcos::byte>(tx.type));
        encodeHeader(out, header(tx));
        encode(out, tx.chainId.value_or(0));
        encode(out, tx.nonce);
        encode(out, tx.maxFeePerGas);  // gasPrice for EIP2930
        encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.accessList);
        encode(out, tx.signatureV);
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
    }
    else if (tx.type == TransactionType::EIP1559)
    {
        // 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value,
        //              data, accessList, signatureYParity, signatureR, signatureS])
        out.push_back(static_cast<bcos::byte>(tx.type));
        encodeHeader(out, header(tx));
        encode(out, tx.chainId.value_or(0));
        encode(out, tx.nonce);
        encode(out, tx.maxPriorityFeePerGas);
        encode(out, tx.maxFeePerGas);
        encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.accessList);
        encode(out, tx.signatureV);
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
    }
    else if (tx.type == TransactionType::EIP4844)
    {
        // 0x03 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value,
        //              data, accessList, maxFeePerBlobGas, blobVersionedHashes,
        //              signatureYParity, signatureR, signatureS])
        out.push_back(static_cast<bcos::byte>(tx.type));
        encodeHeader(out, header(tx));
        encode(out, tx.chainId.value_or(0));
        encode(out, tx.nonce);
        encode(out, tx.maxPriorityFeePerGas);
        encode(out, tx.maxFeePerGas);
        encode(out, tx.gasLimit);
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.accessList);
        encode(out, tx.maxFeePerBlobGas);
        encode(out, tx.blobVersionedHashes);
        encode(out, tx.signatureV);
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
    }
    else
    {
        BCOS_LOG(WARNING) << LOG_BADGE("Web3Transaction") << LOG_DESC("encode unsupported type")
                          << LOG_KV("type", static_cast<uint16_t>(tx.type));
    }
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, AccessListEntry& out) noexcept
{
    return decode(in, out.account, out.storageKeys);
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, Web3Transaction& out) noexcept
{
    return decodeTransaction(in, out, true);
}

bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction& out) noexcept
{
    return decodeTransaction(in, out, false);
}

// Shared helper: verify all list payload bytes were consumed
static bcos::Error::UniquePtr checkListConsumed(
    bcos::bytesRef& in, size_t expectedRemaining) noexcept
{
    if (in.size() != expectedRemaining)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    return nullptr;
}

// Shared helper: decode "to" address (empty means contract creation)
static bcos::Error::UniquePtr decodeToAddress(
    bcos::bytesRef& in, std::optional<bcos::Address>& to) noexcept
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input too short");
    }
    if (in[0] == BYTES_HEAD_BASE)
    {
        to = std::nullopt;
        in = in.getCroppedData(1);
        return nullptr;
    }
    bcos::Address addr{};
    if (auto error = decode(in, addr); error != nullptr)
    {
        return error;
    }
    to.emplace(addr);
    return nullptr;
}

// Legacy/EIP-2930 use maxFeePerGas as gasPrice; keep maxPriorityFeePerGas in sync on decode.
static void mirrorLegacyGasPriceField(rpc::Web3Transaction& tx) noexcept
{
    tx.maxPriorityFeePerGas = tx.maxFeePerGas;
}

// Shared helper: pad signature R/S to 32 bytes; reject overlong components.
static bcos::Error::UniquePtr padSignature(rpc::Web3Transaction& tx) noexcept
{
    if (tx.signatureR.size() > crypto::SECP256K1_SIGNATURE_R_LEN ||
        tx.signatureS.size() > crypto::SECP256K1_SIGNATURE_S_LEN)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooLong, "Signature R/S too long");
    }
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
    return nullptr;
}

// Legacy: rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
static bcos::Error::UniquePtr decodeLegacy(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    auto&& [error, header] = decodeHeader(in);
    if (error != nullptr)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }

    const auto expectedRemaining = in.size() - header.payloadLength;
    out.type = rpc::TransactionType::Legacy;

    // nonce, gasPrice
    if (auto e = decodeItems(in, out.nonce, out.maxFeePerGas); e != nullptr)
    {
        return e;
    }
    mirrorLegacyGasPriceField(out);

    // gasLimit
    if (auto e = decode(in, out.gasLimit); e != nullptr)
    {
        return e;
    }

    // to, value, data
    if (auto e = decodeToAddress(in, out.to); e != nullptr)
    {
        return e;
    }
    if (auto e = decodeItems(in, out.value, out.data); e != nullptr)
    {
        return e;
    }

    if (withSignature)
    {
        if (auto e = decodeItems(in, out.signatureV, out.signatureR, out.signatureS); e != nullptr)
        {
            return e;
        }
        // EIP-155: recover chainId from V
        auto v = out.signatureV;
        if (v == 27 || v == 28)
        {
            // pre EIP-155
            out.chainId = std::nullopt;
            out.signatureV = v - 27;
        }
        else if (v == 0 || v == 1)
        {
            out.chainId = std::nullopt;
        }
        else if (v < 35)
        {
            return BCOS_ERROR_UNIQUE_PTR(InvalidVInSignature, "Invalid V in signature");
        }
        else
        {
            // https://eips.ethereum.org/EIPS/eip-155
            // v = chain_id * 2 + 35 + y_parity
            out.signatureV = (v - 35) % 2;
            out.chainId = ((v - 35) >> 1);
        }
    }
    else
    {
        if (!in.empty())
        {
            uint64_t chainId = 0;
            if (auto e = decode(in, chainId); e != nullptr)
            {
                return e;
            }
            out.chainId.emplace(chainId);
            uint64_t zero1 = 0;
            uint64_t zero2 = 0;
            if (auto e = decodeItems(in, zero1, zero2); e != nullptr)
            {
                return e;
            }
        }
    }

    return checkListConsumed(in, expectedRemaining);
}

// EIP-2930: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
//                        signatureYParity, signatureR, signatureS])
static bcos::Error::UniquePtr decodeEIP2930(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    auto&& [e, header] = decodeHeader(in);
    if (e != nullptr)
    {
        return std::move(e);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }

    const auto expectedRemaining = in.size() - header.payloadLength;
    out.type = rpc::TransactionType::EIP2930;

    // chainId, nonce, gasPrice
    uint64_t chainId = 0;
    if (auto error = decodeItems(in, chainId, out.nonce, out.maxFeePerGas); error != nullptr)
    {
        return error;
    }
    out.chainId.emplace(chainId);
    mirrorLegacyGasPriceField(out);

    // gasLimit, to, value, data, accessList
    if (auto error = decode(in, out.gasLimit); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeToAddress(in, out.to); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeItems(in, out.value, out.data, out.accessList); error != nullptr)
    {
        return error;
    }

    if (withSignature)
    {
        if (auto error = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            error != nullptr)
        {
            return error;
        }
    }
    return checkListConsumed(in, expectedRemaining);
}

// EIP-1559: 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
//                        gasLimit, to, value, data, accessList,
//                        signatureYParity, signatureR, signatureS])
static bcos::Error::UniquePtr decodeEIP1559(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    auto&& [e, header] = decodeHeader(in);
    if (e != nullptr)
    {
        return std::move(e);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }

    const auto expectedRemaining = in.size() - header.payloadLength;
    out.type = rpc::TransactionType::EIP1559;

    // chainId, nonce, maxPriorityFeePerGas
    uint64_t chainId = 0;
    if (auto error = decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
        error != nullptr)
    {
        return error;
    }
    out.chainId.emplace(chainId);

    // maxFeePerGas, gasLimit, to, value, data, accessList
    if (auto error = decode(in, out.maxFeePerGas); error != nullptr)
    {
        return error;
    }
    if (auto error = decode(in, out.gasLimit); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeToAddress(in, out.to); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeItems(in, out.value, out.data, out.accessList); error != nullptr)
    {
        return error;
    }

    if (withSignature)
    {
        if (auto error = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            error != nullptr)
        {
            return error;
        }
    }
    return checkListConsumed(in, expectedRemaining);
}

// EIP-4844: 0x03 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
//                        gasLimit, to, value, data, accessList,
//                        maxFeePerBlobGas, blobVersionedHashes,
//                        signatureYParity, signatureR, signatureS])
static bcos::Error::UniquePtr decodeEIP4844(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    auto&& [e, header] = decodeHeader(in);
    if (e != nullptr)
    {
        return std::move(e);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }

    const auto expectedRemaining = in.size() - header.payloadLength;
    out.type = rpc::TransactionType::EIP4844;

    // chainId, nonce, maxPriorityFeePerGas
    uint64_t chainId = 0;
    if (auto error = decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
        error != nullptr)
    {
        return error;
    }
    out.chainId.emplace(chainId);

    // maxFeePerGas, gasLimit, to, value, data, accessList
    if (auto error = decode(in, out.maxFeePerGas); error != nullptr)
    {
        return error;
    }
    if (auto error = decode(in, out.gasLimit); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeToAddress(in, out.to); error != nullptr)
    {
        return error;
    }
    if (auto error = decodeItems(in, out.value, out.data, out.accessList); error != nullptr)
    {
        return error;
    }

    // blob fields
    if (auto error = decodeItems(in, out.maxFeePerBlobGas, out.blobVersionedHashes);
        error != nullptr)
    {
        return error;
    }

    if (withSignature)
    {
        if (auto error = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
            error != nullptr)
        {
            return error;
        }
    }
    return checkListConsumed(in, expectedRemaining);
}

bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input too short");
    }

    bcos::Error::UniquePtr result = nullptr;

    if (auto const& firstByte = in[0]; 0 < firstByte && firstByte < BYTES_HEAD_BASE)
    {
        // EIP-2718 typed transaction: type_byte || rlp([...])
        const auto txType = magic_enum::enum_cast<rpc::TransactionType>(firstByte);
        if (!txType.has_value())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnsupportedTransactionType, "Unsupported transaction type");
        }
        out.type = txType.value();
        in = in.getCroppedData(1);  // skip type byte

        switch (out.type)
        {
        case rpc::TransactionType::EIP2930:
            result = decodeEIP2930(in, out, withSignature);
            break;
        case rpc::TransactionType::EIP1559:
            result = decodeEIP1559(in, out, withSignature);
            break;
        case rpc::TransactionType::EIP4844:
            result = decodeEIP4844(in, out, withSignature);
            break;
        default:
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnsupportedTransactionType, "Unsupported transaction type");
        }
    }
    else
    {
        // Legacy transaction: rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
        result = decodeLegacy(in, out, withSignature);
    }

    if (result != nullptr)
    {
        return result;
    }

    // Pad signature R/S to 32 bytes
    if (withSignature)
    {
        if (auto padError = padSignature(out); padError != nullptr)
        {
            return padError;
        }
    }

    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected trailing bytes");
    }

    return nullptr;
}
}  // namespace codec::rlp
}  // namespace bcos
