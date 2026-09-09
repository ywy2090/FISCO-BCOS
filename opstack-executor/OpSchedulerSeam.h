// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Engine-facing OP seam. executeBlock exists only for the scheduler concept check.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommitments.h>
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpDepositEncode.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::engine
{

/// Re-exports the engine newPayload surface as dependent names on SchedulerType.
template <class Storage>
class OpSchedulerSeam
{
public:
    explicit OpSchedulerSeam(std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> schedule,
        bcos::evm::opstack::L1BlockInfo l1BlockInfo)
      : m_schedule(std::move(schedule)), m_l1BlockInfo(std::move(l1BlockInfo))
    {
        if (!m_schedule)
        {
            throw std::invalid_argument("OpSchedulerSeam: null fork schedule");
        }
    }

    using BlockEnv = bcos::protocol::BlockHeader;
    using ExecuteResult = OpExecuteBlockResult;
    using ConsensusError = OpConsensusError;
    using StorageError = OpStorageError;
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison.
    static bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }

    /// First mismatching field name, or nullopt.
    static std::optional<std::string> mismatchedFieldOf(
        const OpBlockCommitments& computed, const OpBlockCommitments& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes (needed before execution).
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    [[nodiscard]] bcos::engine::OpForkId forkIdAt(uint64_t timestampSeconds) const
    {
        switch (m_schedule->forkAt(timestampSeconds))
        {
        case bcos::evm::opstack::OpFork::Isthmus:
            return bcos::engine::OpForkId::Isthmus;
        case bcos::evm::opstack::OpFork::Jovian:
            return bcos::engine::OpForkId::Jovian;
        case bcos::evm::opstack::OpFork::Karst:
            return bcos::engine::OpForkId::Karst;
        default:
            throw std::logic_error("OpSchedulerSeam: unsupported schedule fork");
        }
    }

    [[nodiscard]] bcos::engine::EngineApiProfile engineApiFor(uint64_t timestampSeconds) const
    {
        // Engine triple: FCU stays V3, newPayload stays V4, only getPayload bumps V4→V5 at Karst.
        if (forkIdAt(timestampSeconds) == bcos::engine::OpForkId::Karst)
        {
            return bcos::engine::EngineApiProfile{
                .forkchoiceUpdated = bcos::engine::ApiVersion::V3,
                .getPayload = bcos::engine::ApiVersion::V5,
                .newPayload = bcos::engine::ApiVersion::V4,
            };
        }
        return bcos::engine::EngineApiProfile{
            .forkchoiceUpdated = bcos::engine::ApiVersion::V3,
            .getPayload = bcos::engine::ApiVersion::V4,
            .newPayload = bcos::engine::ApiVersion::V4,
        };
    }

    [[nodiscard]] const bcos::evm::opstack::OpForkConfig& configAt(uint64_t timestampSeconds) const
    {
        return m_schedule->configAt(timestampSeconds);
    }

    /// Q5 window: Jovian+ activation live at `blockTsSec` but not at `parentTsSec`.
    /// Both arguments are Unix seconds.
    [[nodiscard]] bool isNoUserTxActivationBlock(uint64_t parentTsSec, uint64_t blockTsSec) const
    {
        return bcos::evm::opstack::isNoUserTxActivationBlock(*m_schedule, parentTsSec, blockTsSec);
    }

    /// `timestampSeconds` is Unix seconds. Callers must convert payload/header internal
    /// milliseconds with `unixSecondsFromInternalMillis`. Never pass raw header.timestamp().
    [[nodiscard]] bcos::engine::EngineForkResolution resolveEngineForkAt(
        uint64_t timestampSeconds) const
    {
        if (timestampSeconds < m_schedule->baselineTimestamp())
        {
            return bcos::engine::OpForkResolutionError::UnsupportedTimestamp;
        }
        const auto forkId = forkIdAt(timestampSeconds);
        const auto& cfg = m_schedule->configAt(timestampSeconds);
        if (forkId == bcos::engine::OpForkId::Karst && cfg.rev != EVMC_OSAKA)
        {
            return bcos::engine::OpForkResolutionError::InconsistentExecutionConfig;
        }
        return bcos::engine::EngineForkContext{
            .forkId = forkId,
            .api = engineApiFor(timestampSeconds),
            .hasDaFootprint = cfg.has_da_footprint,
        };
    }

    /// Synthesize the L1-attributes deposit envelope from the configured L1 info.
    /// Refuses the unset snapshot sentinel (number/time/hash all zero) and an unset
    /// SystemConfig (zero baseFeeScalar or batcherHash) so a missing CL snapshot cannot
    /// mint a plausible L1-attributes deposit.
    /// `timestampSeconds` is the block Unix seconds (FCU attrs / payload), never configAt(0).
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(uint64_t timestampSeconds) const
    {
        if (bcos::evm::opstack::isUnsetL1BlockInfo(m_l1BlockInfo))
        {
            throw std::invalid_argument(
                "OpSchedulerSeam: refuse to synthesize L1-attributes from an unset "
                "L1BlockInfo (number, time, and blockHash are all zero)");
        }
        if (bcos::evm::opstack::isUnsetSystemConfig(m_l1BlockInfo))
        {
            throw std::invalid_argument(
                "OpSchedulerSeam: refuse to synthesize L1-attributes with an unset "
                "SystemConfig (baseFeeScalar and batcherHash must be non-zero)");
        }
        return bcos::evm::opstack::synthesizeL1AttributesDeposit(
            m_l1BlockInfo, configAt(timestampSeconds).has_da_footprint);
    }

    OpSchedulerSeam(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam(OpSchedulerSeam&&) = delete;
    OpSchedulerSeam& operator=(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam& operator=(OpSchedulerSeam&&) = delete;
    ~OpSchedulerSeam() = default;

    /// Concept check only. OP mode never calls this.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error("OpSchedulerSeam::executeBlock: not supported in OP mode");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

private:
    std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> m_schedule;
    bcos::evm::opstack::L1BlockInfo m_l1BlockInfo;
};

}  // namespace bcos::evm::engine
