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
 * @file OpEngineService.inl
 * @brief OP Engine API service implementation (payload build, execute, commit)
 */

#pragma once

// This is the DEFINITION half of the split: OpEngineService.h is declarations-only so an
// installed consumer of the declarations needs no rlp-protocol include dirs (engine links
// rlp-protocol PRIVATE and does not propagate them). Including this .inl is the opt-in
// instantiation point — the template's members use bcos::protocol::EthBlockHeader::computeHash
// (a non-dependent name) and bcos::evm::opstack::estimatedDaSize, so instantiating TUs need
// rlp-protocol and bcos-evm-opstack include dirs and link both (in-tree instantiators do).
#include "OpEngineService.h"
#include <bcos-evm/opstack/RollupCost.h>
#include <opstack-executor/OpCommitments.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/transform.hpp>

namespace bcos::engine
{

namespace detail
{
/// DA-caps unit bridge: the caps count ESTIMATED DA bytes (the Fjord FastLZ size estimate
/// of the sealed envelope), which takes an evmc::bytes_view while the envelope carrier is
/// a bcos::bytes.
inline auto estimatedDaBytes(bcos::bytes const& env)
{
    return bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(env.data(), env.size()));
}

/// release ExecutionPayload keeps a single carrier: `transactions[i].raw`.
inline auto rawEnvelopes(ExecutionPayload const& payload)
{
    return payload.transactions |
           ::ranges::views::transform(
               [](EngineTransaction const& tx) -> bytes const& { return tx.raw; });
}

/// True when the OpExecutionInternalError carries the OpPayloadUndecodable tag:
/// a payload-content fault (an envelope the CL submitted cannot be decoded),
/// not a node-internal fault. Single predicate for both answer shapes — the FCU
/// path maps it to an Invalid FCU status, the newPayload path to an Invalid
/// PayloadStatus; any OTHER OpExecutionInternalError must keep propagating as
/// -32603, never be flattened into a consensus INVALID.
inline bool isUndecodablePayloadFault(OpExecutionInternalError const& error)
{
    return boost::get_error_info<OpPayloadUndecodable>(error) != nullptr;
}

inline std::optional<ForkchoiceUpdatedResult> fcuInvalidIfUndecodable(
    OpExecutionInternalError const& error)
{
    if (!isUndecodablePayloadFault(error))
    {
        return std::nullopt;
    }
    return ForkchoiceUpdatedResult{
        .payloadStatus = engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("undecodable payload transaction envelope")),
        .payloadId = std::nullopt,
    };
}
}  // namespace detail

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
EngineForkContext
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::requireOpEngineForkAt(
    uint64_t timestampSeconds) const
{
    auto const resolved = m_scheduler.resolveEngineForkAt(timestampSeconds);
    if (auto const* ctx = std::get_if<EngineForkContext>(&resolved))
    {
        return *ctx;
    }
    auto const error = std::get<OpForkResolutionError>(resolved);
    if (error == OpForkResolutionError::UnsupportedTimestamp)
    {
        BOOST_THROW_EXCEPTION(UnsupportedFork{} << bcos::errinfo_comment{
                                  "Unsupported timestamp for OP Engine API profile"});
    }
    BOOST_THROW_EXCEPTION(UnsupportedFork{} << bcos::errinfo_comment{
                              "Inconsistent execution config for OP Engine API profile"});
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<GetPayloadResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::getPayload(
    const PayloadID& payloadId, std::uint32_t version)
{
    BuiltPayloadPtr built;
    {
        auto shared = m_tracker.lockShared();
        built = shared.findPayload(payloadId);
    }
    if (!built)
    {
        BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
    }

    // Payload timestamp is internal milliseconds; the seam takes Unix seconds.
    uint64_t const tsSec = unixSecondsFromInternalMillis(built->executionPayload.timestamp);
    auto const ctx = requireOpEngineForkAt(tsSec);
    if (version != static_cast<std::uint32_t>(ctx.api.getPayload))
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{
                "getPayload version does not match the OP Engine API profile at payload "
                "timestamp"});
    }

    engine_common::requireGetPayloadShape(
        built->version, built->executionPayload, built->parentBeaconBlockRoot, version);
    co_return engine_common::assembleGetPayloadData(*built, version);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::updateForkchoice(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
    std::uint32_t version)
{
    if (!isForkchoiceVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                              << bcos::errinfo_comment{"Unsupported Engine API version"});
    }
    std::vector<bcos::bytes> decodedForcedTxs;
    if (payloadAttributes != nullptr)
    {
        if (version < 3)
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "Isthmus+ payload building requires engine_forkchoiceUpdatedV3 "
                    "(JSON-RPC -38005)"});
        }
        // Profile keys on attrs.timestamp (internal ms → Unix seconds), never head.
        // Isthmus/Jovian/Karst all advertise FCU V3, so Karst does not bump this.
        uint64_t const tsSec = unixSecondsFromInternalMillis(payloadAttributes->timestamp);
        auto const ctx = requireOpEngineForkAt(tsSec);
        if (version != static_cast<std::uint32_t>(ctx.api.forkchoiceUpdated))
        {
            BOOST_THROW_EXCEPTION(
                UnsupportedFork{} << bcos::errinfo_comment{
                    "forkchoiceUpdated version does not match the OP Engine API profile "
                    "at attributes timestamp"});
        }
        if (auto validationError = engine_common::validatePayloadAttributes(
                *payloadAttributes, version, &decodedForcedTxs);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
        if (auto validationError = engine_common::op::validateOpPayloadAttributes(
                *payloadAttributes, ctx.hasDaFootprint);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
        if (auto validationError = engine_common::op::requireL1AttributesDeposit(
                *payloadAttributes, m_allowSynthesizedL1Attributes);
            validationError.has_value())
        {
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                .payloadId = std::nullopt,
            };
        }
    }

    auto view = m_globalStateStorage.fork();
    auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
        view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
    // S6: an imported (not-yet-canonical) head is a legal FCU target — the OP lookup
    // falls through to the ImportedStore before answering SYNCING (design §4.2 row 2).
    bool headIsImported = false;
    if (!headBlockNumber.has_value())
    {
        if (auto importedHead = m_importedStore.get(forkchoiceState.headBlockHash);
            importedHead.has_value())
        {
            headIsImported = true;
            headBlockNumber = importedHead->number;
        }
        else
        {
            // Unknown head: SYNCING and nothing else (§4.5; CL resets, not success).
            co_return ForkchoiceUpdatedResult{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
                .payloadId = std::nullopt,
            };
        }
    }
    // SetCanonical BEFORE the safe/finalized resolution (geth order; design §4.2:
    // "SetCanonical 先于 safe/finalized 检查") — the head is imported, so it cannot
    // already be the canonical hash of its height (import never writes canonical keys).
    bcos::protocol::BlockNumber canonicalTipNumber = -1;
    if (headIsImported)
    {
        co_await canonicalizeImportedHead(forkchoiceState.headBlockHash);
        // Fresh view over the new canonical flat: the safe/finalized checks below must
        // resolve on the NEW chain, not the pre-SetCanonical one.
        view = m_globalStateStorage.fork();
        headBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
        if (!headBlockNumber.has_value())
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize did not make the head canonical"});
        }
        canonicalTipNumber = *headBlockNumber;
    }
    else
    {
        canonicalTipNumber = co_await bcos::ledger::getCurrentBlockNumber(
            view, bcos::ledger::fromStorage);
        // SetCanonical for a LEDGER-canonical head above the tip pointer (skipped
        // middle heights are already canonical rows): advance SYS_CURRENT_STATE —
        // a stale tip pointer under a canonical head means SetCanonical's
        // number-move has not happened yet (design §4.2: 沿新链写满).
        auto canonicalHeadHashEarly = co_await bcos::ledger::getBlockHash(
            view, *headBlockNumber, bcos::ledger::fromStorage);
        if (canonicalHeadHashEarly.has_value() &&
            *canonicalHeadHashEarly == forkchoiceState.headBlockHash &&
            *headBlockNumber > canonicalTipNumber)
        {
            // Write the row straight into the backend. NOT mergeView/mergeBackStorage:
            // those merge the OLDEST queued layer whenever the pending deque is
            // non-empty (MultiLayerStorage.h's FIFO warning), which would commit
            // another block's in-flight layer from inside an FCU.
            auto row = std::make_shared<typename GlobalStateStorageType::MutableStorage>();
            bcos::storage::Entry numberEntry;
            numberEntry.set(std::to_string(*headBlockNumber));
            co_await storage2::writeOne(*row,
                executor_v1::StateKey{bcos::ledger::SYS_CURRENT_STATE,
                    bcos::ledger::SYS_KEY_CURRENT_NUMBER},
                std::move(numberEntry));
            co_await m_globalStateStorage.mergeToBackends(*row);
            canonicalTipNumber = *headBlockNumber;
            if (m_delegate)
            {
                m_delegate->canonicalizedTo(*headBlockNumber);
            }
        }
    }
    // All-zero safe/finalized hashes are the Engine-API "not set" value: skip number
    // resolution and canonical checks for that field (op-geth SetSafe/SetFinalized are
    // only called for non-zero hashes). A missing HEAD is SYNCING; a non-zero
    // unresolvable safe/finalized is InvalidForkchoiceState (op-geth, finding BJ).
    bool const safeSet = forkchoiceState.safeBlockHash != bcos::h256{};
    bool const finalizedSet = forkchoiceState.finalizedBlockHash != bcos::h256{};
    auto safeBlockNumber = safeSet ? co_await bcos::ledger::getBlockNumber(view,
                                         forkchoiceState.safeBlockHash, bcos::ledger::fromStorage) :
                                     std::nullopt;
    auto finalizedBlockNumber =
        finalizedSet ? co_await bcos::ledger::getBlockNumber(
                           view, forkchoiceState.finalizedBlockHash, bcos::ledger::fromStorage) :
                       std::nullopt;

    if (!headBlockNumber.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus =
                makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
            .payloadId = std::nullopt,
        };
    }
    if ((safeSet && !safeBlockNumber.has_value()) ||
        (finalizedSet && !finalizedBlockNumber.has_value()))
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe or finalized block is unknown"});
    }

    auto canonicalHeadHash =
        co_await bcos::ledger::getBlockHash(view, *headBlockNumber, bcos::ledger::fromStorage);
    bool const headCanonical =
        canonicalHeadHash.has_value() && *canonicalHeadHash == forkchoiceState.headBlockHash;
    // Same-number safe/finalized already resolved above: their canonical hash is the
    // head's (one NUMBER_2_HASH row per height), so reuse it instead of a second storage
    // read; zero (unset) fields skip resolution entirely. Heartbeat FCUs (all three
    // hashes equal) drop from 3 to 1 sequential reads.
    auto canonicalSafeHash =
        (!safeSet || *safeBlockNumber == *headBlockNumber) ?
            canonicalHeadHash :
            co_await bcos::ledger::getBlockHash(view, *safeBlockNumber, bcos::ledger::fromStorage);
    auto canonicalFinalizedHash = (!finalizedSet || *finalizedBlockNumber == *headBlockNumber) ?
                                      canonicalHeadHash :
                                      co_await bcos::ledger::getBlockHash(
                                          view, *finalizedBlockNumber, bcos::ledger::fromStorage);

    ResolvedForkchoice resolved{
        .state = forkchoiceState,
        .headNumber = *headBlockNumber,
        .safeNumber = safeBlockNumber,
        .finalizedNumber = finalizedBlockNumber,
        .headCanonical = headCanonical,
        .payloadAttributesPresent = payloadAttributes != nullptr,
        .safeCanonical = engine_common::forkchoiceHashIsCanonical(
            forkchoiceState.safeBlockHash, canonicalSafeHash),
        .finalizedCanonical = engine_common::forkchoiceHashIsCanonical(
            forkchoiceState.finalizedBlockHash, canonicalFinalizedHash),
        .allowNonLinearHead = true,
        .canonicalTipNumber = canonicalTipNumber,
    };
    const auto applyResult = m_tracker.applyForkchoice(resolved);
    if (applyResult == ForkchoiceApplyResult::Swallowed)
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
    }

    ForkchoiceUpdatedResult result{
        .payloadStatus =
            makeStatus(PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
        .payloadId = std::nullopt,
    };
    if (payloadAttributes == nullptr)
    {
        co_return result;
    }

    co_return co_await buildOpPayload(
        forkchoiceState, *payloadAttributes, version, *headBlockNumber + 1, decodedForcedTxs);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<ForkchoiceUpdatedResult>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpPayload(
    const ForkchoiceState& forkchoiceState, const PayloadAttributes& payloadAttributes,
    std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
    std::vector<bcos::bytes> const& decodedForcedTxs)
{
    // Same policy as EthEngineService (option B): deterministic derivePayloadId, not a
    // process-local sequence counter. Reuse validate's decoded forced txs (finding AE).
    // The id's version byte is the PAYLOAD SHAPE version (V3/V4-method → PayloadV3),
    // matching both the cache entry's version below and upstream: op-geth's
    // ForkchoiceUpdatedV3/V4 build the same PayloadV3 shape, so the same content under
    // either method must derive the same id (GetPayloadV4 accepts only PayloadV3 ids).
    auto payloadIdOpt = engine_common::derivePayloadId(payloadAttributes,
        forkchoiceState.headBlockHash, engine_common::payloadShapeVersion(version),
        decodedForcedTxs);
    if (!payloadIdOpt.has_value())
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("payloadAttributes.transactions contains undecodable hex")),
            .payloadId = std::nullopt,
        };
    }
    auto payloadId = *payloadIdOpt;

    u256 baseFee;
    uint64_t parentTsSec = 0;
    {
        auto view = m_globalStateStorage.fork();
        auto parentNumberStr = boost::lexical_cast<std::string>(nextBlockNumber - 1);
        auto parentHeaderEntry = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
        if (!parentHeaderEntry.has_value())
        {
            // Parent hash already resolved (canonical). Missing header is local-state
            // corruption — fail closed rather than pricing the block at 1 gwei.
            co_return ForkchoiceUpdatedResult{
                .payloadStatus = makeStatus(PayloadValidationStatus::Invalid,
                    forkchoiceState.headBlockHash,
                    std::string("parent block header is missing from storage")),
                .payloadId = std::nullopt,
            };
        }
        auto stored = parentHeaderEntry->get();
        bcos::bytes parentHeaderBytes(stored.begin(), stored.end());
        auto parentHeader =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
        parentTsSec = unixSecondsFromInternalMillis(
            static_cast<uint64_t>(parentHeader->timestamp()));
        baseFee = calcOpBaseFee(*parentHeader, m_scheduler.configAt(parentTsSec).has_da_footprint);
    }

    requireDelegate();

    // Q5 builder half: activation blocks are deposits-only. Skip the mempool
    // (treat as noTxPool) and FCU-INVALID any non-deposit already in attrs.
    // Hash-less execute rejects used to flatten to -32603 here.
    uint64_t const attrsTsSec = unixSecondsFromInternalMillis(payloadAttributes.timestamp);
    bool const activation = m_scheduler.isNoUserTxActivationBlock(parentTsSec, attrsTsSec);
    if (activation)
    {
        for (auto const& env : decodedForcedTxs)
        {
            if (dispatchRawTransaction(bcos::ref(env)) != RawTransactionKind::Deposit)
            {
                co_return ForkchoiceUpdatedResult{
                    .payloadStatus = makeStatus(PayloadValidationStatus::Invalid,
                        forkchoiceState.headBlockHash,
                        std::string("op block: unexpected non-deposit transactions in fork "
                                    "activation block")),
                    .payloadId = std::nullopt,
                };
            }
        }
    }

    auto sealView = m_globalStateStorage.fork();
    std::vector<protocol::Transaction::Ptr> sealedTxs;
    if (!activation && !payloadAttributes.noTxPool.value_or(false))
    {
        sealView.newMutable();
        m_memPool.remove(sealView);
        m_memPool.seal(m_blockTxCountLimit, sealView, std::back_inserter(sealedTxs));
    }

    std::vector<bytes> forcedEnvelopes;
    // Reached only when tests set allowSynthesizedL1Attributes. Production
    // op_engine_rpc never invents this envelope (op-geth does not either).
    if (!payloadAttributes.transactions.has_value() || payloadAttributes.transactions->empty())
    {
        forcedEnvelopes.push_back(m_scheduler.synthesizeL1AttributesEnvelope(
            unixSecondsFromInternalMillis(payloadAttributes.timestamp)));
    }
    if (payloadAttributes.transactions.has_value())
    {
        forcedEnvelopes.insert(
            forcedEnvelopes.end(), decodedForcedTxs.begin(), decodedForcedTxs.end());
    }

    std::vector<std::pair<crypto::HashType, bytes>> sealedEnvelopes;
    std::vector<std::string> sealedSenders;
    std::vector<std::optional<std::uint64_t>> sealedNonces;
    sealedEnvelopes.reserve(sealedTxs.size());
    sealedSenders.reserve(sealedTxs.size());
    sealedNonces.reserve(sealedTxs.size());
    for (auto& sealedTx : sealedTxs)
    {
        if (sealedTx->type() !=
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        {
            BCOS_LOG(WARNING) << LOG_BADGE("OpEngineService")
                              << LOG_DESC(
                                     "buildOpPayload: excluding transaction without an "
                                     "EIP-2718 wire form");
            continue;
        }
        sealedEnvelopes.emplace_back(
            sealedTx->hash(), bcostars::protocol::reassembleWeb3RawTransaction(
                                  sealedTx->extraTransactionBytes(), sealedTx->signatureData()));
        sealedSenders.emplace_back(sealedTx->sender());
        sealedNonces.emplace_back(bcos::safeFromQuantity(sealedTx->nonce()));
    }

    std::set<crypto::HashType> evicted;
    // op-geth miner: excluding nonce n of sender S also drops S's later nonces from
    // this candidate and never evicts those successors from the pool (R3-F1).
    // Walk by nonce, not sealed-vector position (finding BU): seal order is not a
    // nonce-order contract the build path may assume.
    auto skipSenderTail = [&](crypto::HashType const& hash) {
        evicted.insert(hash);
        std::string sender;
        std::optional<std::uint64_t> culpritNonce;
        for (std::size_t i = 0; i < sealedEnvelopes.size(); ++i)
        {
            if (sealedEnvelopes[i].first == hash)
            {
                sender = sealedSenders[i];
                culpritNonce = sealedNonces[i];
                break;
            }
        }
        if (sender.empty() || !culpritNonce.has_value())
        {
            return;
        }
        for (std::size_t i = 0; i < sealedEnvelopes.size(); ++i)
        {
            if (sealedSenders[i] != sender || sealedEnvelopes[i].first == hash)
            {
                continue;
            }
            if (sealedNonces[i].has_value() && *sealedNonces[i] > *culpritNonce)
            {
                evicted.insert(sealedEnvelopes[i].first);
            }
        }
    };
    if (m_daCaps && m_daCaps->maxTxSize.load(std::memory_order_relaxed) != 0)
    {
        // DACaps count ESTIMATED DA bytes — the Fjord FastLZ size estimate of the sealed
        // envelope (op-geth's RollupCostData().EstimatedDASize()), never the raw envelope
        // length. While the cap is unset (zero = uncapped) skip the estimate entirely:
        // FastLZ over every sealed envelope on every attempt would tax the uncapped
        // default path for nothing.
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (!m_daCaps->txFits(detail::estimatedDaBytes(env)))
            {
                skipSenderTail(hash);
            }
        }
    }

    auto const parentBeaconBlockRoot = payloadAttributes.parentBeaconBlockRoot.value();

    auto assemblePayload = [&](std::vector<bytes> candidateEnvelopes) {
        std::vector<EngineTransaction> candidateTransactions;
        candidateTransactions.reserve(candidateEnvelopes.size());
        for (auto& env : candidateEnvelopes)
        {
            // Move: env is a non-const ref into the by-value candidates vector, consumed
            // here; the candidate bytes become the payload's single carrier unchanged.
            candidateTransactions.push_back(
                EngineTransaction{.raw = std::move(env), .decoded = nullptr});
        }
        ExecutionPayload candidate{
            .logsBloom = Bloom{},
            .parentHash = forkchoiceState.headBlockHash,
            .stateRoot = h256{},
            .receiptsRoot = h256{},
            .prevRandao = payloadAttributes.prevRandao,
            .gasLimit = u256(payloadAttributes.gasLimit.value()),
            .gasUsed = 0,
            .baseFeePerGas = baseFee,
            .blockHash = h256{},
            .transactions = std::move(candidateTransactions),
            .extraData = detail::encodeOptimismExtraData(payloadAttributes),
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .timestamp = payloadAttributes.timestamp,
            .blockNumber = nextBlockNumber,
            .withdrawals = std::vector<WithdrawalV1>{},
            .blobGasUsed = u256(0),
            .excessBlobGas = u256(0),
            .blockAccessList = std::nullopt,
            .slotNumber = std::nullopt,
            .withdrawalsRoot = h256{},
        };
        return candidate;
    };

    ExecutionPayload payload;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    // Retry loop: each evicted culprit re-executes the whole candidate block from scratch
    // (no incremental prefix reuse), so k failing pool txs cost up to k+1 full build+execute
    // passes plus the always-on canonical verify pass. Bounded by the sealed-envelope count;
    // only reworked when per-envelope execution becomes reusable (finding AX).
    while (true)
    {
        std::vector<bytes> candidateEnvelopes = forcedEnvelopes;
        std::optional<bcos::engine::DACaps::Budget> budget;
        if (m_daCaps && m_daCaps->maxBlockSize.load(std::memory_order_relaxed) != 0)
        {
            // Same estimated-DA unit as txFits above: the block-size budget accumulates
            // the forced (undroppable) envelopes' estimates and admits each sealed tx by
            // its estimate, not its raw length.
            std::uint64_t forcedBytes = 0;
            for (auto const& env : forcedEnvelopes)
            {
                forcedBytes += detail::estimatedDaBytes(env);
            }
            budget.emplace(*m_daCaps, forcedBytes);
        }
        for (auto const& [hash, env] : sealedEnvelopes)
        {
            if (evicted.count(hash) != 0)
            {
                continue;
            }
            if (budget && !budget->admits(detail::estimatedDaBytes(env)))
            {
                break;
            }
            candidateEnvelopes.push_back(env);
        }
        payload = assemblePayload(std::move(candidateEnvelopes));

        const auto transactionsRoot =
            SchedulerType::computeTxRoot(detail::rawEnvelopes(payload));
        auto provisionalHeader = engine_common::op::rebuildOpEthHeader(
            m_blockFactory->blockHeaderFactory(), payload, transactionsRoot, parentBeaconBlockRoot);
        bcos::protocol::Block::Ptr block;
        try
        {
            block = buildOpBlock(payload, provisionalHeader);
        }
        catch (const OpExecutionInternalError& e)
        {
            if (auto invalid = detail::fcuInvalidIfUndecodable(e))
            {
                co_return *invalid;
            }
            throw;
        }

        bcos::Error::Ptr resetError;
        m_delegate->reset([&](bcos::Error::Ptr error) { resetError = std::move(error); });
        if (resetError)
        {
            // The build loop's whole model rests on reset having done its documented effect
            // (dropping any uncommitted pending, restoring the watermark) before executeBlock
            // runs; a failed reset must not be silently ignored.
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("OP payload build reset failed: ") + resetError->errorMessage()});
        }
        bcos::Error::Ptr executeError;
        m_delegate->executeBlock(block, /*verify=*/false,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
                executeError = std::move(error);
                executedHeader = std::move(header);
            });
        if (!executeError && executedHeader)
        {
            break;
        }
        auto const message =
            executeError ? executeError->errorMessage() : std::string("no executed header");
        auto culprit = executeError ? culpritTxHashFromError(*executeError) :
                                      std::optional<crypto::HashType>{};
        // A block-gas capacity fault means the tx is VALID but does not fit this
        // candidate (mempool seals by count only). OpRejectIsCapacity's contract is
        // "skip this build, do not evict": the tx must stay in the mempool for a later
        // block, so the eviction loop only excludes it from this candidate (finding AY).
        bool const isCapacityReject =
            executeError != nullptr &&
            boost::get_error_info<bcos::engine::OpRejectIsCapacity>(*executeError) != nullptr;
        if (culprit.has_value() && evicted.count(*culprit) == 0 &&
            std::any_of(sealedEnvelopes.begin(), sealedEnvelopes.end(),
                [&culprit](auto const& entry) { return entry.first == *culprit; }))
        {
            skipSenderTail(*culprit);
            if (!isCapacityReject)
            {
                std::array<crypto::HashType, 1> hashSpan{*culprit};
                m_memPool.removeByHash(std::span<crypto::HashType const>(hashSpan));
            }
            continue;
        }
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  std::string("OP payload build execution failed: ") + message});
    }

    payload.stateRoot = executedHeader->stateRoot();
    payload.receiptsRoot = executedHeader->receiptsRoot();
    payload.gasUsed = u256(executedHeader->gasUsed());
    {
        auto executedBloom = executedHeader->logsBloom();
        std::copy(executedBloom.begin(), executedBloom.end(), payload.logsBloom.begin());
    }
    payload.withdrawalsRoot = executedHeader->withdrawalsRoot();
    if (auto executedBlobGas = executedHeader->blobGasUsed())
    {
        payload.blobGasUsed = *executedBlobGas;
    }
    auto finalHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            SchedulerType::computeTxRoot(detail::rawEnvelopes(payload)), parentBeaconBlockRoot);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*finalHeader);

    bcos::protocol::Block::Ptr finalBlock;
    try
    {
        finalBlock = buildOpBlock(payload, finalHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        if (auto invalid = detail::fcuInvalidIfUndecodable(e))
        {
            co_return *invalid;
        }
        throw;
    }
    bcos::Error::Ptr resetError;
    m_delegate->reset([&](bcos::Error::Ptr error) { resetError = std::move(error); });
    if (resetError)
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
            std::string("OP payload build reset failed: ") + resetError->errorMessage()});
    }
    bcos::Error::Ptr canonicalError;
    bcos::protocol::BlockHeader::Ptr canonicalHeader;
    m_delegate->executeBlock(finalBlock, /*verify=*/true,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, bool) {
            canonicalError = std::move(error);
            canonicalHeader = std::move(header);
        });
    if (canonicalError || !canonicalHeader)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                std::string("OP payload build canonical pass failed: ") +
                (canonicalError ? canonicalError->errorMessage() : "no executed header")});
    }

    auto commonEntry = std::make_shared<BuiltPayload>();
    commonEntry->version = engine_common::payloadShapeVersion(version);
    commonEntry->executionPayload = std::move(payload);
    commonEntry->blockValue = 0;
    commonEntry->blobsBundle = std::nullopt;
    commonEntry->shouldOverrideBuilder = false;
    commonEntry->parentBeaconBlockRoot = parentBeaconBlockRoot;
    if (engine_common::payloadShapeVersion(version) == static_cast<std::uint32_t>(ApiVersion::V3))
    {
        commonEntry->blobsBundle = BlobsBundleV1{};
    }

    OpPayloadArtifacts stagedArtifact{.canonicalHeader = std::move(canonicalHeader)};
    {
        // Copy the hash before the entry is moved: publishBuiltPayload stores the
        // payload by move, which leaves the original object's blockHash member
        // moved-from, and putStaged then hashes that reference into hashToId.
        const auto blockHash = commonEntry->executionPayload.blockHash;
        auto guard = m_tracker.lockExclusive();
        publishBuiltPayload(guard, m_artifacts, payloadId, blockHash,
            std::move(commonEntry), std::move(stagedArtifact));
    }

    co_return ForkchoiceUpdatedResult{
        .payloadStatus =
            makeStatus(PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
        .payloadId = payloadId,
    };
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus>
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::newPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    co_return co_await handleOpNewPayload(request, version);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<PayloadStatus> OpEngineService<MemPoolType, GlobalStateStorageType,
    SchedulerType>::handleOpNewPayload(const NewPayloadRequest& request, std::uint32_t version)
{
    if (!isNewPayloadVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{
                "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)"});
    }
    // Isthmus+ newPayload stays V4 (Karst does not bump). Profile newPayload is
    // V4 at every fork; a second timestamp-gated comparison is dead and would
    // also change V3 + illegal-timestamp from UnsupportedFork into a fork error.

    try
    {
        co_return co_await runOpNewPayloadSteps(request);
    }
    catch (const OpExecutionInternalError&)
    {
        throw;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                "OP newPayload threw an unclassified exception outside block execution "
                "(validation, comparison or registration phase)"});
    }
}

/// Header-level commitment snapshot for the engine-side import gate (the same field
/// set OpScheduler's verify arm compares via mismatchedFieldOf). Namespace-scope
/// inline: the .inl parses in TUs that never instantiate the consumer template.
inline bcos::evm::engine::OpBlockCommitments commitmentsOfHeader(
    bcos::protocol::BlockHeader const& h)
{
    auto bloom = h.logsBloom();
    bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
    std::optional<uint64_t> blobGasUsed;
    if (auto bg = h.blobGasUsed())
    {
        blobGasUsed = static_cast<uint64_t>(bcos::u256(*bg));
    }
    return bcos::evm::engine::OpBlockCommitments{
        .receiptsRoot = h.receiptsRoot(),
        .logsBloom = logsBloom,
        .withdrawalsRoot = h.withdrawalsRoot().value_or(bcos::h256{}),
        .stateRoot = h.stateRoot(),
        .gasUsed = h.gasUsed(),
        .txRoot = h.txsRoot(),
        .blobGasUsed = blobGasUsed,
        .requestsHash = h.requestsHash(),
    };
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
    task::Task<PayloadStatus> OpEngineService<MemPoolType, GlobalStateStorageType,
        SchedulerType>::runOpNewPayloadSteps(const NewPayloadRequest& request)
{
    // No reset of m_lastExecutedHeader here: a duplicate newPayload
    // arriving while another one is mid-flight must not clear a header the
    // concurrent success just published. Assignment happens only on the success
    // paths, so a failed run simply leaves the previous payload's header — the
    // "last executed" semantics the accessor documents.
    auto const& payload = request.executionPayload;

    if (auto validationError = engine_common::op::validateOpNewPayloadRequest(request,
            m_scheduler
                .configAt(unixSecondsFromInternalMillis(payload.timestamp))
                .has_da_footprint);
        validationError.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
    }

    const auto transactionsRoot = SchedulerType::computeTxRoot(detail::rawEnvelopes(payload));
    const auto ethHeader =
        engine_common::op::rebuildOpEthHeader(m_blockFactory->blockHeaderFactory(), payload,
            transactionsRoot, *request.parentBeaconBlockRoot);
    if (bcos::protocol::EthBlockHeader::computeHash(*ethHeader) != payload.blockHash)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("blockHash does not match the reconstructed block header"));
    }

    {
        bcos::protocol::BlockHeader::Ptr builtHeader;
        {
            auto shared = m_tracker.lockShared();
            builtHeader = detail::findBuiltHeader(shared, m_artifacts, payload.blockHash);
        }
        if (builtHeader)
        {
            auto knownView = m_globalStateStorage.fork();
            if (auto known = co_await bcos::ledger::getBlockNumber(
                    knownView, payload.blockHash, bcos::ledger::fromStorage);
                known.has_value())
            {
                co_return makeStatus(
                    PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
            }
            requireDelegate();
            bcos::Error::Ptr commitError;
            m_delegate->commitBlock(
                builtHeader, [&](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
                    commitError = std::move(error);
                });
            if (commitError)
            {
                co_return mapDelegateError(*commitError, std::nullopt);
            }
            {
                std::lock_guard lock(m_lastExecutedHeaderMutex);
                m_lastExecutedHeader = builtHeader;
            }
            co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
        }
    }

    auto view = m_globalStateStorage.fork();
    // OP parent lookup (design §4.1): canonical chain first, then the ImportedStore.
    // Missing in BOTH is the only parent-shape SYNCING (§4.5, no ACCEPTED) — the old
    // "known but not canonical → SYNCING" dead-end is gone: an imported parent
    // extends the block tree.
    const auto latestValidHash = std::make_optional(payload.parentHash);
    std::optional<bcos::protocol::BlockNumber> canonicalParentNumber =
        co_await bcos::ledger::getBlockNumber(view, payload.parentHash, bcos::ledger::fromStorage);
    bcos::protocol::BlockNumber parentBlockNumber = -1;
    bcos::protocol::BlockHeader::Ptr parentHeader;
    if (canonicalParentNumber.has_value())
    {
        parentBlockNumber = *canonicalParentNumber;
        const auto parentNumberStr = boost::lexical_cast<std::string>(*canonicalParentNumber);
        auto parentHeaderEntry = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, parentNumberStr});
        if (!parentHeaderEntry.has_value())
        {
            // Parent hash already resolved and is canonical. Skipping timestamp / baseFee
            // here would accept a payload we cannot price — fail closed.
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("parent block header is missing from storage"));
        }
        const auto storedHeader = parentHeaderEntry->get();
        bcos::bytes parentHeaderBytes(storedHeader.begin(), storedHeader.end());
        parentHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(parentHeaderBytes);
        if (parentHeader->number() != static_cast<int64_t>(*canonicalParentNumber))
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "stored parent block header height mismatch"});
        }
    }
    else if (auto importedParent = m_importedStore.get(payload.parentHash))
    {
        // Parent header BY HASH from the store — never NUMBER_2_BLOCK_HEADER[number]
        // (a same-height canonical sibling would masquerade as the parent).
        parentBlockNumber = importedParent->number;
        try
        {
            parentHeader = m_blockFactory->blockHeaderFactory()->createBlockHeader(
                importedParent->headerBytes);
        }
        catch (const std::exception& e)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      std::string("imported parent block header is undecodable: ") +
                                      e.what()});
        }
    }
    else
    {
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    if (payload.blockNumber != parentBlockNumber + 1)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("blockNumber must be exactly one greater than the parent's"));
    }

    if (static_cast<uint64_t>(payload.timestamp) <=
        static_cast<uint64_t>(parentHeader->timestamp()))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("timestamp must be strictly greater than the parent's"));
    }

    {
        auto expectedBaseFee = calcOpBaseFee(*parentHeader,
            m_scheduler
                .configAt(unixSecondsFromInternalMillis(
                    static_cast<uint64_t>(parentHeader->timestamp())))
                .has_da_footprint);
        if (payload.baseFeePerGas != expectedBaseFee)
        {
            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
                std::string("baseFeePerGas does not match the value computed "
                            "from the parent"));
        }
    }

    // Idempotent replay: a hash that already landed — canonical or imported — is
    // VALID without re-execution (§4.5 同哈希已落下).
    if (auto knownBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, payload.blockHash, bcos::ledger::fromStorage);
        knownBlockNumber.has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }
    if (m_importedStore.hasBlock(payload.blockHash))
    {
        co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
    }

    const auto childNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);
    if (auto occupiedHeight = co_await storage2::readOne(
            view, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, childNumberStr});
        occupiedHeight.has_value() && !canonicalParentNumber.has_value())
    {
        // Occupied height whose parent is IMPORTED: the imported slot already has
        // descendants — overwriting would orphan their state (§4.2 单分叉冲突) →
        // SYNCING. A CANONICAL parent (the ancestor-sibling case, Task 7) imports
        // fine: the block lands by hash and FCU decides canonicality later.
        co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    // ONE plane decision for both parent flavours (canonical-below-tip ancestor sibling
    // and imported-parent chained import). The committed flat IS the parent plane
    // exactly when the parent is the canonical tip (canonicalTip == -1 means no
    // committed chain yet, where the committed plane is the genesis plane). Otherwise
    // the parent's materialized post-state is required, and its absence is SYNCING —
    // design §4.5's "hasState failed" row — never a wrong-plane execution on the
    // committed flat.
    auto const canonicalTip =
        co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
    std::vector<bcos::protocol::BlockHeader::Ptr> parentHeaders;
    std::shared_ptr<void> parentFlat;
    const bool parentIsCanonicalTip =
        canonicalParentNumber.has_value() &&
        (canonicalTip == -1 || *canonicalParentNumber == canonicalTip);
    if (!parentIsCanonicalTip)
    {
        auto parent = m_importedStore.get(payload.parentHash);
        if (!m_importedStore.hasState(payload.parentHash) || !parent.has_value() ||
            parent->postStateFlat == nullptr)
        {
            BCOS_LOG(INFO) << LOG_BADGE("OP_ENGINE")
                           << "SYNCING: parent post-state plane unavailable at height "
                           << (payload.blockNumber - 1);
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        parentFlat = parent->postStateFlat;
        parentHeaders.push_back(
            m_blockFactory->blockHeaderFactory()->createBlockHeader(parent->headerBytes));
    }

    requireDelegate();

    // Payload-content fault: an envelope the CL submitted cannot be decoded into a
    // transaction. op-geth answers INVALID at block construction for this class; it is
    // not a node-internal fault, so it must not surface as -32603 (finding AM). Internal
    // faults (storage, delegate) still throw and map to -32603 by the caller.
    bcos::protocol::Block::Ptr block;
    try
    {
        block = buildOpBlock(payload, ethHeader);
    }
    catch (const OpExecutionInternalError& e)
    {
        // Same tag discipline as the two FCU build sites: only the
        // tagged payload-content fault maps to a consensus INVALID; any other
        // OpExecutionInternalError must keep propagating as -32603.
        if (!detail::isUndecodablePayloadFault(e))
        {
            throw;
        }
        // Stable Engine API string only (finding CG). FCU already returns this
        // exact phrase via fcuInvalidIfUndecodable; dump Boost diagnostics in logs,
        // not in validationError.
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("undecodable payload transaction envelope"));
    }

    // S5: import, not commit. The import plane is the PARENT's post-state: committed
    // flat when the parent is the canonical tip, otherwise the parent's stored
    // materialized flat (canonical-ancestor sibling / chained import). No canonical
    // table is written (design §4.2 newPayload condition 1).
    bcos::Error::Ptr executeError;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    std::shared_ptr<void> blockDelta;
    std::shared_ptr<void> blockFlat;
    m_delegate->importExecute(block, parentHeaders, parentFlat,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header,
            std::shared_ptr<void> delta, std::shared_ptr<void> flat)
        {
            executeError = std::move(error);
            executedHeader = std::move(header);
            blockDelta = std::move(delta);
            blockFlat = std::move(flat);
        });
    if (executeError)
    {
        co_return mapDelegateError(*executeError, latestValidHash);
    }
    if (!executedHeader || !executedHeader->withdrawalsRoot().has_value())
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("executed header is missing withdrawalsRoot"));
    }
    if (executedHeader->withdrawalsRoot() != payload.withdrawalsRoot)
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("withdrawalsRoot does not match the executed header"));
    }
    // The scheduler's verify arm is not on the import path; the engine owns the
    // commitment gate (design §4.2 newPayload VALID condition 2 — the full
    // mismatchedFieldOf set, gasUsed/logsBloom included). A mismatch is a payload
    // fault: INVALID + parent, and the block is NOT stored (§4.5).
    if (auto mismatch = bcos::evm::engine::mismatchedFieldOf(
            commitmentsOfHeader(*executedHeader), commitmentsOfHeader(*ethHeader)))
    {
        co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
            std::string("commitment mismatch on field ") + *mismatch);
    }

    // Land by hash. No canonical key is written here — FCU owns SetCanonical.
    bcos::bytes importedHeaderBytes;
    ethHeader->encode(importedHeaderBytes);
    ImportedBlock imported{.hash = payload.blockHash,
        .parent = payload.parentHash,
        .number = payload.blockNumber,
        .headerBytes = std::move(importedHeaderBytes),
        .storageDelta = std::move(blockDelta),
        .postStateFlat = std::move(blockFlat)};
    for (auto const& env : detail::rawEnvelopes(payload))
    {
        imported.txs.push_back(env);
        imported.txHashes.push_back(m_blockFactory->cryptoSuite()->hashImpl()->hash(env));
    }
    // Canonical-row payloads: tars-encoded transactions (SYS_HASH_2_TX) and encoded
    // receipts (SYS_HASH_2_RECEIPT) — importExecute attached the receipts to the
    // block (commitPersist convention), so capture them here for canonicalize.
    for (auto&& receiptView : block->receipts())
    {
        auto receipt = std::move(receiptView).toShared();
        bcos::bytes encoded;
        receipt->encode(encoded);
        imported.receipts.push_back(std::move(encoded));
    }
    for (auto txView : block->transactions())
    {
        auto tx = std::move(txView).toShared();
        bcos::bytes encoded;
        tx->encode(encoded);
        imported.encodedTxs.push_back(std::move(encoded));
    }
    // Same-height occupant check (§4.2 单分叉冲突 vs §4.3 ancestor sibling): an
    // imported-live occupant with descendants must not lose its ancestor; a
    // CANONICAL occupant may be shadowed — its descendants stay on the canonical
    // chain until FCU switches labels.
    // Design §4.2: the occupancy DECISION and the put must be one atomic step, but no
    // await may sit under a POSIX lock (resumption can move threads and make the
    // unlock UB — the file's standing rule). So: read the ledger unlocked, then take
    // the lock for the sync-only decide+put, re-checking that no competing import
    // landed on this height in between (a compare-and-set on the occupancy).
    auto const observedOccupant = m_importedStore.occupantAt(payload.blockNumber);
    bool occupantCanonical = false;
    if (observedOccupant.has_value() && *observedOccupant != payload.blockHash)
    {
        auto occNumber = co_await bcos::ledger::getBlockNumber(
            view, *observedOccupant, bcos::ledger::fromStorage);
        occupantCanonical = occNumber.has_value() && *occNumber == payload.blockNumber;
        if (!occupantCanonical)
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }
    {
        std::lock_guard treeLock(m_importedTreeMutex);
        if (m_importedStore.occupantAt(payload.blockNumber) != observedOccupant)
        {
            // Another thread imported at this height while we were reading the ledger;
            // its occupant's canonicality is unknown here → retry-safe SYNCING.
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
        if (!m_importedStore.put(std::move(imported), occupantCanonical))
        {
            co_return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
        }
    }

    {
        std::lock_guard lock(m_lastExecutedHeaderMutex);
        m_lastExecutedHeader = executedHeader;
    }
    co_return makeStatus(PayloadValidationStatus::Valid, payload.blockHash, std::nullopt);
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
void OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::
    pruneFlatsAtOrBelowFinalized()
{
    // Memory bound (review F4): a block at/below the finalized marker can never be the
    // parent plane of a NEW import (op-node's promoteFinalized makes it irreversible),
    // so its materialized flat is dead weight. Absent a finalized marker the live
    // window is (tip .. tip] plus the unfinalized suffix — unbounded until the CL
    // finalizes, which the design's no-prune milestone accepted.
    if (auto finalized = m_tracker.finalizedBlockNumber(); finalized.has_value())
    {
        m_importedStore.pruneFlatsAtOrBelow(*finalized);
    }
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
task::Task<void> OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::
    canonicalizeImportedHead(const h256& headHash)
{
    namespace detail = bcos::evm::engine::detail;
    using MutableStorageT = typename GlobalStateStorageType::MutableStorage;
    // Design §4.2 engine lock over the whole switch/forward canonicalize. The body
    // awaits storage merges, and a guard spanning an await is only sound while the
    // task stays on this thread; this follows the file's established shape (see
    // OpScheduler's execute/commit guards, which document the same premise) and fails
    // CLOSED when the lock is busy rather than queueing behind it.
    std::unique_lock treeLock(m_importedTreeMutex, std::try_to_lock);
    if (!treeLock.owns_lock())
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "canonicalize: another import/canonicalize is in flight"});
    }

    // Collect the chain head → ... → child-of-canonical (store walk); the parent is
    // canonical when HASH_2_NUMBER resolves it (import never writes that key).
    std::vector<ImportedBlock> chain;  // genesis-side first after the reverse below
    {
        auto view = m_globalStateStorage.fork();
        auto cursor = m_importedStore.get(headHash);
        if (!cursor.has_value())
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize: head is not in the ImportedStore"});
        }
        while (true)
        {
            chain.push_back(*cursor);
            auto const parentNumber = co_await bcos::ledger::getBlockNumber(
                view, cursor->parent, bcos::ledger::fromStorage);
            if (parentNumber.has_value())
            {
                break;
            }
            auto parent = m_importedStore.get(cursor->parent);
            if (!parent.has_value())
            {
                // A switch/reorg SetCanonical (head not a linear extension of the
                // canonical tip) needs the §4.4.5 replay — not the forward merge.
                BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                          "canonicalize: imported chain does not root in the "
                                          "canonical chain (switch case is Task 7)"});
            }
            cursor = parent;
        }
    }
    std::reverse(chain.begin(), chain.end());

    // SWITCH detection: the head's height is at/below the current canonical tip —
    // the canonical chain must be ROLLED BACK to the fork point before overlaying.
    // Restoring the head's materialized post-state flat wholesale (captured at
    // import) replaces the backend observably: same accounts, same storage, no C-era
    // keys surviving (design §4.4.5's "整表替换"; the memory-flat is this
    // milestone's stand-in for the production MPT replay from finalized).
    auto viewForTip = m_globalStateStorage.forkCommitted();
    auto const currentTip = co_await bcos::ledger::getCurrentBlockNumber(
        viewForTip, bcos::ledger::fromStorage);
    if (currentTip != -1 && chain.back().number <= currentTip)
    {
        auto const headBlock = chain.back();
        auto flat =
            std::static_pointer_cast<typename GlobalStateStorageType::MutableStorage>(
                headBlock.postStateFlat);
        if (!flat)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize switch: head has no post-state flat"});
        }

        auto& backend = m_globalStateStorage.m_latestBackend;

        // (1) Remove backend rows absent from the head flat (C-era keys: accounts,
        // storage slots, the canonical rows above/at heights that moved).
        {
            // The collection iterator is scoped: MemoryStorage's range() holds the
            // storage lock for the iterator's lifetime, so mutating the same storage
            // before it is destroyed self-deadlocks on that lock.
            std::vector<executor_v1::StateKey> doomed;
            {
                auto backendIterator = co_await backend.range();
                while (true)
                {
                    auto item = co_await backendIterator.next();
                    if (!item.has_value())
                    {
                        break;
                    }
                    auto const& backendKey = std::get<0>(*item);
                    auto const tableAndKey = std::string_view(backendKey.m_tableAndKey);
                    auto present = co_await storage2::readOne(*flat,
                        executor_v1::StateKeyView(tableAndKey.substr(0, backendKey.m_split),
                            tableAndKey.substr(backendKey.m_split + 1)));
                    if (!present.has_value())
                    {
                        doomed.push_back(
                            executor_v1::StateKey(backendKey.m_tableAndKey));
                    }
                }
            }
            for (auto const& key : doomed)
            {
                auto const tableAndKey = std::string_view(key.m_tableAndKey);
                co_await storage2::removeOne(backend,
                    executor_v1::StateKeyView(tableAndKey.substr(0, key.m_split),
                        tableAndKey.substr(key.m_split + 1)));
            }
        }

        // (2) Write/overwrite every head-flat row.
        {
            auto flatIterator = co_await flat->range();
            while (true)
            {
                auto item = co_await flatIterator.next();
                if (!item.has_value())
                {
                    break;
                }
                auto& [stateKeyRef, valueVariant] = *item;
                if (auto* entry = std::get_if<bcos::storage::Entry>(&valueVariant))
                {
                    co_await storage2::writeOne(backend,
                        executor_v1::StateKey(stateKeyRef.m_tableAndKey), std::move(*entry));
                }
            }
        }

        // (3) Canonical rows for the new head + trim above it.
        bcos::storage::Entry headNumberEntry;
        headNumberEntry.set(std::to_string(headBlock.number));
        co_await storage2::writeOne(backend,
            executor_v1::StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                bcos::concepts::bytebuffer::toView(headBlock.hash)},
            std::move(headNumberEntry));
        bcos::storage::Entry hashEntry;
        hashEntry.set(headBlock.hash.asBytes());
        co_await storage2::writeOne(backend,
            executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH,
                std::to_string(headBlock.number)},
            std::move(hashEntry));
        bcos::storage::Entry headerEntry;
        headerEntry.set(headBlock.headerBytes);
        co_await storage2::writeOne(backend,
            executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                std::to_string(headBlock.number)},
            std::move(headerEntry));
        bcos::storage::Entry numberEntry;
        numberEntry.set(std::to_string(headBlock.number));
        co_await storage2::writeOne(backend,
            executor_v1::StateKey{bcos::ledger::SYS_CURRENT_STATE,
                bcos::ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(numberEntry));
        for (std::size_t i = 0; i < headBlock.encodedTxs.size(); ++i)
        {
            bcos::storage::Entry txEntry;
            txEntry.set(headBlock.encodedTxs[i]);
            co_await storage2::writeOne(backend,
                executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                    bcos::concepts::bytebuffer::toView(headBlock.txHashes[i])},
                std::move(txEntry));
            if (i < headBlock.receipts.size())
            {
                bcos::storage::Entry receiptEntry;
                receiptEntry.set(headBlock.receipts[i]);
                co_await storage2::writeOne(backend,
                    executor_v1::StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                        bcos::concepts::bytebuffer::toView(headBlock.txHashes[i])},
                    std::move(receiptEntry));
            }
        }
        for (auto k = headBlock.number + 1; k <= currentTip; ++k)
        {
            co_await storage2::removeOne(backend,
                executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_HASH,
                    std::to_string(k)));
            co_await storage2::removeOne(backend,
                executor_v1::StateKeyView(bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                    std::to_string(k)));
        }

        // Design §4.2 post-condition: the relabelled tip must really be backed by the
        // head's world state — this is the check that catches a stale/partial plane.
        if (m_delegate)
        {
            auto headHeader =
                m_blockFactory->blockHeaderFactory()->createBlockHeader(headBlock.headerBytes);
            m_delegate->verifyCanonicalStateRoot(headHeader->stateRoot());
            m_delegate->canonicalizedTo(headBlock.number);
        }
        m_importedStore.pruneFlatsAbove(headBlock.number);
        pruneFlatsAtOrBelowFinalized();
        co_return;
    }

    using MutableStorageT = typename GlobalStateStorageType::MutableStorage;
    for (auto& block : chain)
    {
        auto delta = std::static_pointer_cast<MutableStorageT>(block.storageDelta);
        if (!delta)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize: imported block has no storage delta"});
        }
        auto header =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(block.headerBytes);
        if (header->number() != block.number)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "canonicalize: stored header height mismatch"});
        }

        // This height's canonical keys ride the SAME merge as the block's delta
        // (一块一配): HASH_2_NUMBER / NUMBER_2_HASH / NUMBER_2_BLOCK_HEADER, plus
        // SYS_CURRENT_STATE on the head's own merge.
        bcos::storage::Entry numberEntry;
        numberEntry.set(std::to_string(block.number));
        co_await storage2::writeOne(*delta,
            executor_v1::StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                bcos::concepts::bytebuffer::toView(block.hash)},
            std::move(numberEntry));
        bcos::storage::Entry hashEntry;
        hashEntry.set(block.hash.asBytes());
        co_await storage2::writeOne(*delta,
            executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH,
                std::to_string(block.number)},
            std::move(hashEntry));
        bcos::storage::Entry headerEntry;
        headerEntry.set(block.headerBytes);
        co_await storage2::writeOne(*delta,
            executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                std::to_string(block.number)},
            std::move(headerEntry));
        // Canonical tx/receipt rows (prewriteBlockToBuffer's phase-2 equivalent,
        // writeNonces=false): keyed by tx hash, index-aligned with the block's txs.
        for (std::size_t i = 0; i < block.encodedTxs.size(); ++i)
        {
            bcos::storage::Entry txEntry;
            txEntry.set(block.encodedTxs[i]);
            co_await storage2::writeOne(*delta,
                executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                    bcos::concepts::bytebuffer::toView(block.txHashes[i])},
                std::move(txEntry));
            if (i < block.receipts.size())
            {
                bcos::storage::Entry receiptEntry;
                receiptEntry.set(block.receipts[i]);
                co_await storage2::writeOne(*delta,
                    executor_v1::StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                        bcos::concepts::bytebuffer::toView(block.txHashes[i])},
                    std::move(receiptEntry));
            }
        }
        if (block.hash == headHash)
        {
            bcos::storage::Entry currentEntry;
            currentEntry.set(std::to_string(block.number));
            co_await storage2::writeOne(*delta,
                executor_v1::StateKey{bcos::ledger::SYS_CURRENT_STATE,
                    bcos::ledger::SYS_KEY_CURRENT_NUMBER},
                std::move(currentEntry));
        }
        // The imported chain never occupies the MLS pending deque — mergeToBackends
        // (design §4.2: 不要对空 deque 调 mergeBackStorage).
        co_await m_globalStateStorage.mergeToBackends(*delta);
    }

    m_importedStore.pruneFlatsAbove(chain.back().number);
    pruneFlatsAtOrBelowFinalized();
    if (m_delegate)
    {
        auto headHeader =
            m_blockFactory->blockHeaderFactory()->createBlockHeader(chain.back().headerBytes);
        m_delegate->verifyCanonicalStateRoot(headHeader->stateRoot());
        m_delegate->canonicalizedTo(chain.back().number);
    }
}

template <class MemPoolType, class GlobalStateStorageType, class SchedulerType>
bcos::protocol::Block::Ptr
OpEngineService<MemPoolType, GlobalStateStorageType, SchedulerType>::buildOpBlock(
    const ExecutionPayload& payload, bcos::protocol::BlockHeader::Ptr header)
{
    auto block = m_blockFactory->createBlock();
    block->setBlockHeader(std::move(header));
    auto& hashImpl = *m_blockFactory->cryptoSuite()->hashImpl();
    for (auto const& env : detail::rawEnvelopes(payload))
    {
        const auto txHash = hashImpl.hash(env);
        auto tarsTx = engine_common::op::opEnvelopeToTars(env, txHash);
        if (!tarsTx)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << OpPayloadUndecodable{true}
                                                             << bcos::errinfo_comment{
                                                                    "undecodable payload "
                                                                    "transaction envelope"});
        }
        tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tars = std::move(*tarsTx)]() mutable { return &tars; });
        block->appendTransaction(std::move(tx));
    }
    return block;
}

}  // namespace bcos::engine
