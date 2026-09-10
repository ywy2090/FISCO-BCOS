/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file OpEngineService.cpp
 * @brief OP Engine API service validators (OP payload-attribute and newPayload-request validation)
 */

#include "OpEngineService.h"

#include "EngineServiceCommon.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <limits>

namespace bcos::engine::engine_common::op
{
namespace
{
constexpr char const* c_opMaxBlockGasLimitMessage =
    "gasLimit exceeds the maximum block gas limit (2^63-1)";

constexpr bool gasLimitExceedsOpCap(std::uint64_t gasLimit) noexcept
{
    return gasLimit > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

// Consensus header constants live in engine_common (EngineServiceCommon.h) — shared with
// the Eth builder so the keccak256(rlp(header))-critical literals have exactly one home.
}  // namespace

std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash)
{
    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = bcos::codec::rlp::decode(envRef, web3Tx); err)
    {
        return std::nullopt;
    }
    if (!envRef.empty())
    {
        return std::nullopt;
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    if (tarsTx.sender.empty())
    {
        try
        {
            auto sender = bcos::fromHex(web3Tx.sender());
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const&)
        {
            return std::nullopt;
        }
    }
    return tarsTx;
}

void applyOpHeaderConstants(bcos::protocol::BlockHeader& header)
{
    header.setUncleHash(engine_common::c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(engine_common::c_posNonce);
}

std::vector<std::string> supportedOpCapabilities()
{
    // The OP lane's implemented window, NOT the Eth list. With the payload-timestamp
    // profile (engineApiFor / extraDataLayoutFor) the live method now varies by fork:
    // newPayloadV2 from Canyon up, V3 Ecotone/Fjord/Granite/Holocene, V4 Isthmus+;
    // getPayloadV2/V3 likewise, V4 Isthmus+, V5 Karst. Advertising exactly what the
    // profile can select keeps a CL from picking a method this lane rejects (-38005)
    // on every call. Still absent: newPayloadV1 (op-node starts at V2 — Bedrock is
    // its first fork), newPayloadV5 (does not exist upstream), FCU V4 (unimplemented
    // and absent upstream). op-geth advertises by reflection over every method it
    // implements (ExchangeCapabilities), i.e. also never a fork-trimmed subset.
    static const std::vector<std::string> caps{"engine_exchangeCapabilities",
        "engine_forkchoiceUpdatedV1", "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
    return caps;
}

std::optional<std::uint64_t> narrowU256ToU64(const u256& value)
{
    static const u256 maxU64(std::numeric_limits<std::uint64_t>::max());
    if (value > maxU64)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

bcos::h2048 toEthLogsBloom(const Bloom& logsBloom)
{
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, OpForkId forkId)
{
    if (!payloadAttributes.gasLimit.has_value())
    {
        return std::string("gasLimit parameter is required (OP rollup)");
    }
    // Same 2^63-1 cap as validateOpNewPayloadRequest. JSON parseQuantity already
    // rejects >uint64; this closes the window where FCU would stamp a payloadId
    // that newPayload then refuses (op-geth defers the cap to VerifyHeader).
    if (gasLimitExceedsOpCap(*payloadAttributes.gasLimit))
    {
        return std::string(c_opMaxBlockGasLimitMessage);
    }
    // The fork's extraData layout decides which 1559 fields these attributes may
    // carry (op-geth checkOptimismPayloadAttributes): pre-Holocene has neither,
    // Holocene adds the 8-byte params, Jovian adds minBaseFee.
    auto const layout = extraDataLayoutFor(forkId);
    if (layout == OpExtraDataLayout::Empty)
    {
        if (payloadAttributes.eip1559Params.has_value())
        {
            return std::string("eip1559Params is not allowed before the Holocene fork");
        }
    }
    else
    {
        if (!payloadAttributes.eip1559Params.has_value())
        {
            return std::string("eip1559Params is required on the OP path (Holocene+)");
        }
        if (payloadAttributes.eip1559Params->size() != 8)
        {
            return std::string("eip1559Params must be exactly 8 bytes");
        }
        const auto [denominator, elasticity] =
            bcos::engine::decodeEip1559Params(*payloadAttributes.eip1559Params);
        if (auto error = engine_common::validateHolocene1559Params(denominator, elasticity))
        {
            return error;
        }
    }
    if (payloadAttributes.withdrawals.has_value() && !payloadAttributes.withdrawals->empty())
    {
        return std::string("withdrawals must be empty on the OP path");
    }
    if (layout == OpExtraDataLayout::Jovian17)
    {
        if (!payloadAttributes.minBaseFee.has_value())
        {
            return std::string("minBaseFee is required after the Jovian fork");
        }
    }
    else if (payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee must be null before the Jovian fork");
    }
    return std::nullopt;
}

std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, OpForkId forkId, std::uint32_t version)
{
    const auto& payload = request.executionPayload;

    // release ExecutionPayload uses a single carrier: transactions[i].raw (no dual
    // rawTransactions mirror). Empty list is valid (deposit-only / empty blocks).
    for (std::size_t i = 0; i < payload.transactions.size(); ++i)
    {
        if (payload.transactions[i].raw.empty())
        {
            return "executionPayload.transactions[" + std::to_string(i) + "] is empty";
        }
        if (auto error = engine_common::validateRawTransactionKind(
                dispatchRawTransaction(bcos::ref(payload.transactions[i].raw)), i))
        {
            return error;
        }
    }
    // Shanghai's withdrawal list appears at Canyon, so "absent" and "present but
    // empty" are different contracts: Bedrock/Regolith payloads omit the field
    // entirely (op-geth NewPayloadV2 before Shanghai expects nil, after it empty).
    if (forkId >= OpForkId::Canyon)
    {
        if (!payload.withdrawals.has_value() || !payload.withdrawals->empty())
        {
            return std::string("withdrawals must be present and empty on the OP path");
        }
    }
    else if (payload.withdrawals.has_value())
    {
        return std::string("withdrawals must be absent before the Canyon fork");
    }
    if (!request.expectedBlobVersionedHashes.empty())
    {
        return std::string("expectedBlobVersionedHashes must be an empty array on the OP path");
    }
    // Cancun's fields arrive with Ecotone (V3): the beacon root and the blob pair.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        if (!request.parentBeaconBlockRoot.has_value())
        {
            return std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3+");
        }
        if (!payload.excessBlobGas.has_value() || *payload.excessBlobGas != 0)
        {
            return std::string("excessBlobGas must be present and zero on the OP path");
        }
        if (!payload.blobGasUsed.has_value())
        {
            return std::string("blobGasUsed must be present on the OP path");
        }
    }
    else
    {
        if (request.parentBeaconBlockRoot.has_value())
        {
            return std::string("parentBeaconBlockRoot must be absent before the Ecotone fork");
        }
        if (payload.excessBlobGas.has_value() || payload.blobGasUsed.has_value())
        {
            return std::string("blob gas fields must be absent before the Ecotone fork");
        }
    }
    // Prague's additions arrive with Isthmus (V4): the withdrawals root and the
    // (present-but-empty) execution requests list.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4))
    {
        if (!payload.withdrawalsRoot.has_value())
        {
            return std::string("withdrawalsRoot is required on the OP path (Isthmus+)");
        }
        // Same reasoning as the Eth sibling (EngineServiceImpl.h): the wire already
        // enforces the fourth newPayloadV4 parameter (parseNewPayloadRequest always
        // sets the list for V4), so accepting a missing list here would hand
        // in-process callers a laxer Isthmus contract than the wire.
        if (!request.executionRequests.has_value() || !request.executionRequests->empty())
        {
            return std::string("executionRequests must be a present-but-empty list on the OP path");
        }
    }
    else
    {
        if (payload.withdrawalsRoot.has_value())
        {
            return std::string("withdrawalsRoot must be absent before the Isthmus fork");
        }
        if (request.executionRequests.has_value())
        {
            return std::string("executionRequests must be absent before the Isthmus fork");
        }
    }
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    if (gasLimitExceedsOpCap(*narrowU256ToU64(payload.gasLimit)))
    {
        return std::string(c_opMaxBlockGasLimitMessage);
    }
    if (auto error = validateOpExtraDataForLayout(payload.extraData, extraDataLayoutFor(forkId)))
    {
        return "executionPayload.extraData " + *error;
    }
    if (!narrowU256ToU64(payload.gasUsed).has_value())
    {
        return std::string("gasUsed exceeds the uint64 range of the ETH header field");
    }
    // blobGasUsed is guaranteed present by the V3+ branch above.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        if (!narrowU256ToU64(*payload.blobGasUsed).has_value())
        {
            return std::string("blobGasUsed exceeds the uint64 range of the ETH header field");
        }
        // The DA footprint only enters the fee from Jovian (max(gasUsed, blobGasUsed)),
        // so before that the field must be zero.
        if (forkId < OpForkId::Jovian && *payload.blobGasUsed != 0)
        {
            return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
        }
        // Jovian meters the DA footprint, so an oversized one is a reject from there on.
        if (forkId >= OpForkId::Jovian && *payload.blobGasUsed > payload.gasLimit)
        {
            return std::string("DA footprint (blobGasUsed) exceeds the block gas limit");
        }
    }
    return std::nullopt;
}

bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, std::optional<h256> const& parentBeaconBlockRoot, OpForkId forkId)
{
    // Intentionally NO setEthBlockVersion (unlike detail::finalizeEthBlockHeader): the OP
    // header is a FISCO BlockHeader whose ethBlockVersion stays NON_ETH, which is exactly
    // the header class EthBlockHeader::computeHash documents itself for ("block-identity
    // hash for FISCO-native/OP headers ... that validateHeader rejects"). The RLP encoding
    // cannot depend on that field: the ctor builds EthBlockHeaderData from field presence
    // (each optional fork field copied when set) and the shared codec encodes exactly the
    // set optionals positionally — EthBlockHeaderData carries no version input at all. With
    // every fork field stamped below, the encoding is the full 21-field form op-geth
    // produces, and the external-oracle golden test
    // (op_golden_vector_rebuild_matches_op_geth_block_hash, vendored corpus) pins it byte
    // for byte. calculateRLPHash (validateHeader path) is not usable on these headers by
    // design; finalizeEthBlockHeader needs setEthBlockVersion only because it goes through
    // calculateRLPHash on the Eth lane.
    auto header = factory->createBlockHeader();
    const auto number = static_cast<bcos::protocol::BlockNumber>(payload.blockNumber);
    header->setNumber(number);
    header->setTimestamp(static_cast<int64_t>(payload.timestamp));
    header->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = payload.parentHash});
    header->setCoinbase(payload.feeRecipient);
    header->setStateRoot(payload.stateRoot);
    header->setTxsRoot(transactionsRoot);
    header->setReceiptsRoot(payload.receiptsRoot);
    const auto bloom = toEthLogsBloom(payload.logsBloom);
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    header->setGasLimit(payload.gasLimit);
    header->setGasUsed(payload.gasUsed);
    header->setExtraData(payload.extraData);
    header->setPrevRandao(payload.prevRandao);
    header->setBaseFee(payload.baseFeePerGas);
    // Header fields appear with their Ethereum fork, exactly as detail::finalizeEthBlockHeader
    // does on the Eth lane: a pre-Canyon block carries no withdrawals hash, a pre-Ecotone
    // block no blob pair or beacon root, a pre-Isthmus block no requests hash. Setting only
    // what the fork defines is what makes the RLP match op-geth, whose corresponding header
    // fields are optional/nil there. OP blocks carry no withdrawals, so the hash is always
    // the empty-trie root.
    if (forkId >= OpForkId::Canyon)
    {
        header->setWithdrawalsRoot(bcos::engine::detail::withdrawalsRootFor(payload));
    }
    if (forkId >= OpForkId::Ecotone)
    {
        // Both are guaranteed by validateOpNewPayloadRequest (V3+) / the accepted attrs
        // (V3 build), so .value() here matches detail::finalizeEthBlockHeader's own
        // precondition style rather than silently building a hash for a bogus block.
        header->setBlobGasUsed(payload.blobGasUsed.value());
        header->setExcessBlobGas(bcos::u256(0));
        header->setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
    }
    if (forkId >= OpForkId::Isthmus)
    {
        header->setRequestsHash(engine_common::c_emptyRequestsHash);
    }
    applyOpHeaderConstants(*header);
    return header;
}

}  // namespace bcos::engine::engine_common::op
