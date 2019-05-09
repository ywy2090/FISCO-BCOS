/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file Transaction.cpp
 * @author Gav Wood <i@gavwood.com>, chaychen, asherli
 * @date 2018
 */

#include "Transaction.h"
#include "EVMSchedule.h"
#include "Exceptions.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/vector_ref.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Exceptions.h>

using namespace std;
using namespace dev;
// using namespace dev::crypto;
using namespace dev::eth;
Transaction::Transaction(bytesConstRef _rlpData, CheckTransaction _checkSig)
{
    m_rpcCallback = nullptr;
    decode(_rlpData, _checkSig);
}

void Transaction::decode(bytesConstRef tx_bytes, CheckTransaction _checkSig)
{
    m_rlpBuffer.assign(tx_bytes.data(), tx_bytes.data() + tx_bytes.size());
    RLP const rlp(tx_bytes);
    decode(rlp, _checkSig);
}

void Transaction::decode(RLP const& rlp, CheckTransaction _checkSig)
{
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        decodeRC2(rlp, _checkSig);
    }
    else
    {
        decodeRC1(rlp, _checkSig);
    }
}

void Transaction::decodeRC1(RLP const& rlp, CheckTransaction _checkSig)
{
    try
    {
        if (!rlp.isList())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("rc1 transaction RLP must be a list"));

        m_nonce = rlp[0].toInt<u256>();
        m_gasPrice = rlp[1].toInt<u256>();
        m_gas = rlp[2].toInt<u256>();
        m_blockLimit = rlp[3].toInt<u256>();
        m_type = rlp[4].isEmpty() ? ContractCreation : MessageCall;
        m_receiveAddress = rlp[4].isEmpty() ? Address() : rlp[4].toHash<Address>(RLP::VeryStrict);
        m_value = rlp[5].toInt<u256>();

        if (!rlp[6].isData())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("rc1 transaction data RLP must be an array"));

        m_data = rlp[6].toBytes();

        // v -> rlp[7].toInt<NumberVType>() - VBase;  // 7
        // r -> rlp[8].toInt<u256>();             // 8
        // s -> rlp[9].toInt<u256>();             // 9

        // decode v r s by increasing rlp index order for faster decoding
        NumberVType v = rlp[7].toInt<NumberVType>() - VBase;
        u256 r = rlp[8].toInt<u256>();
        u256 s = rlp[9].toInt<u256>();

        m_vrs = SignatureStruct(r, s, v);

        if (_checkSig >= CheckTransaction::Cheap && !m_vrs->isValid())
            BOOST_THROW_EXCEPTION(InvalidSignature());

        if (_checkSig == CheckTransaction::Everything)
            m_sender = sender();
    }
    catch (Exception& _e)
    {
        _e << errinfo_name(
            "invalid rc1 transaction format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
        throw;
    }
}

void Transaction::decodeRC2(RLP const& rlp, CheckTransaction _checkSig)
{
    try
    {
        if (!rlp.isList())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("rc2 transaction RLP must be a list"));

        m_nonce = rlp[0].toInt<u256>();
        m_gasPrice = rlp[1].toInt<u256>();
        m_gas = rlp[2].toInt<u256>();
        m_blockLimit = rlp[3].toInt<u256>();
        m_type = rlp[4].isEmpty() ? ContractCreation : MessageCall;
        m_receiveAddress = rlp[4].isEmpty() ? Address() : rlp[4].toHash<Address>(RLP::VeryStrict);
        m_value = rlp[5].toInt<u256>();

        if (!rlp[6].isData())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("rc2 transaction data RLP must be an array"));

        m_data = rlp[6].toBytes();
        m_chainId = rlp[7].toInt<u256>();
        m_groupId = rlp[8].toInt<u256>();
        m_extraData = rlp[9].toBytes();

        // decode v r s by increasing rlp index order for faster decoding
        NumberVType v = rlp[10].toInt<NumberVType>() - VBase;
        u256 r = rlp[11].toInt<u256>();
        u256 s = rlp[12].toInt<u256>();

        m_vrs = SignatureStruct(r, s, v);

        if (_checkSig >= CheckTransaction::Cheap && !m_vrs->isValid())
            BOOST_THROW_EXCEPTION(InvalidSignature());

        if (_checkSig == CheckTransaction::Everything)
            m_sender = sender();
    }
    catch (Exception& _e)
    {
        _e << errinfo_name(
            "invalid rc2 transaction format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
        throw;
    }
}

Address const& Transaction::safeSender() const noexcept
{
    try
    {
        return sender();
    }
    catch (...)
    {
        return ZeroAddress;
    }
}

Address const& Transaction::sender() const
{
    if (!m_sender)
    {
        if (!m_vrs)
            BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

        auto p = recover(*m_vrs, sha3(WithoutSignature));
        if (!p)
            BOOST_THROW_EXCEPTION(InvalidSignature());
        m_sender = right160(dev::sha3(bytesConstRef(p.data(), sizeof(p))));
    }
    return m_sender;
}

SignatureStruct const& Transaction::signature() const
{
    if (!m_vrs)
        BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

    return *m_vrs;
}

/// encode the transaction to bytes
void Transaction::encode(bytes& _trans, IncludeSignature _sig) const
{
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        encodeRC2(_trans, _sig);
    }
    else
    {
        encodeRC1(_trans, _sig);
    }
}

void Transaction::encodeRC1(bytes& _trans, IncludeSignature _sig) const
{
    RLPStream _s;
    if (m_type == NullTransaction)
        return;
    _s.appendList((_sig ? c_sigCount : 0) + c_fieldCountRC1WithOutSig);
    _s << m_nonce << m_gasPrice << m_gas << m_blockLimit;
    if (m_type == MessageCall)
        _s << m_receiveAddress;
    else
        _s << "";
    _s << m_value << m_data;

    if (_sig)
    {
        if (!m_vrs)
            BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

        m_vrs->encode(_s);
    }

    _s.swapOut(_trans);
}

void Transaction::encodeRC2(bytes& _trans, IncludeSignature _sig) const
{
    RLPStream _s;
    if (m_type == NullTransaction)
        return;
    _s.appendList((_sig ? c_sigCount : 0) + c_fieldCountRC2WithOutSig);
    _s << m_nonce << m_gasPrice << m_gas << m_blockLimit;
    if (m_type == MessageCall)
        _s << m_receiveAddress;
    else
        _s << "";
    _s << m_value << m_data << m_chainId << m_groupId << m_extraData;

    if (_sig)
    {
        if (!m_vrs)
            BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

        m_vrs->encode(_s);
    }

    _s.swapOut(_trans);
}

static const u256 c_secp256k1n(
    "115792089237316195423570985008687907852837564279074904382605163141518161494337");

void Transaction::checkLowS() const
{
    if (!m_vrs)
        BOOST_THROW_EXCEPTION(TransactionIsUnsigned());
    m_vrs->check();
}

int64_t Transaction::baseGasRequired(
    bool _contractCreation, bytesConstRef _data, EVMSchedule const& _es)
{
    int64_t g = _contractCreation ? _es.txCreateGas : _es.txGas;

    // Calculate the cost of input data.
    // No risk of overflow by using int64 until txDataNonZeroGas is quite small
    // (the value not in billions).
    for (auto i : _data)
        g += i ? _es.txDataNonZeroGas : _es.txDataZeroGas;
    return g;
}

h256 Transaction::sha3(IncludeSignature _sig) const
{
    if (_sig == WithSignature && m_hashWith)
        return m_hashWith;

    bytes s;
    encode(s, _sig);

    auto ret = dev::sha3(s);
    if (_sig == WithSignature)
        m_hashWith = ret;
    return ret;
}

void Transaction::updateTransactionHashWithSig(dev::h256 const& txHash)
{
    m_hashWith = txHash;
}

void Transaction::triggerRpcCallback(LocalisedTransactionReceipt::Ptr pReceipt) const
{
    try
    {
        if (m_rpcCallback)
            m_rpcCallback(pReceipt);
    }
    catch (std::exception& e)
    {
        // LOG(ERROR) << "callback RPC callback failed";
        return;
    }
}

bool Transaction::checkChainIdAndGroupId(u256 _chainId, u256 _groupId)
{
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        return (_chainId == m_chainId && _groupId == m_groupId);
    }
    else
    {
        return true;
    }
}
