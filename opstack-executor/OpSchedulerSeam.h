// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerSeam — the engine-facing seam shim for OP mode. `executeBlock` exists only to
// satisfy the scheduler_v1::TransactionScheduler concept check (OP mode never calls it — throws
// immediately). A pure template header, no .cpp.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockExecute.h>  // computeOpTxRoot / announcedCommitmentsOf
#include <opstack-executor/OpCommitments.h>  // OpBlockCommitments / commitmentsOf / mismatchedFieldOf
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope (Tier-2 build)
#include <cstdint>
#include <limits>
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

/// The OP block-execution environment was folded into `protocol::BlockHeader` (it now carries all
/// former OpBlockEnv fields); the engine fills one header object and the decoders read the
/// accessors directly.

namespace detail
{
}  // namespace detail
/// OP scheduler component: a pure engine-facing seam shim, constructed once per immutable
/// OpForkSchedule (composition-root-owned). It only re-publishes the seam surface the engine
/// reaches as dependent names on `SchedulerType`.
template <class Storage>
class OpSchedulerSeam
{
public:
    explicit OpSchedulerSeam(std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> schedule)
      : m_schedule(std::move(schedule))
    {
        if (!m_schedule)
        {
            throw std::invalid_argument("OpSchedulerSeam: null fork schedule");
        }
    }

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a dependent name on its
    // `SchedulerType` template parameter — the only channel available (the engine must not include
    // anything from bcos-evm). The definitions live in OpCommon.h / OpBlockExecute.h; this block
    // re-publishes them under the class scope the engine can reach.

    /// The block-execution environment the engine fills in from the payload — the FISCO
    /// protocol::BlockHeader itself.
    using BlockEnv = bcos::protocol::BlockHeader;
    /// What executeOpBlock returns.
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection → engine maps to INVALID.
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure → engine maps to JSON-RPC -32603, never INVALID.
    using StorageError = OpStorageError;
    /// s_eth_hash_2_rawtx. No longer written (registerOpBlock writes SYS_HASH_2_TX via
    /// opEnvelopeToTars); kept only for read-side test assertions.
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (plus the two seal-only outputs) in bcos:: types.
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison (re-published as a dependent name).
    static bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }

    /// First mismatching field name, or nullopt (re-published as a dependent name).
    static std::optional<std::string> mismatchedFieldOf(
        const OpBlockCommitments& computed, const OpBlockCommitments& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes — the engine needs it before execution to
    /// reconstruct the header for the blockHash check (ExecutionPayload carries no such field).
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
        const auto forkId = forkIdAt(timestampSeconds);
        if (forkId == bcos::engine::OpForkId::Karst)
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

    [[nodiscard]] bool hasDaFootprintAt(uint64_t timestampSeconds) const
    {
        return m_schedule->configAt(timestampSeconds).has_da_footprint;
    }

    [[nodiscard]] const bcos::evm::opstack::OpForkConfig& configAt(uint64_t timestampSeconds) const
    {
        return m_schedule->configAt(timestampSeconds);
    }

    [[nodiscard]] bcos::engine::EngineForkResolution resolveEngineForkAt(
        uint64_t timestampSeconds) const
    {
        if (timestampSeconds < m_schedule->baselineTimestamp())
        {
            return bcos::engine::OpForkResolutionError::UnsupportedTimestamp;
        }
        const auto forkId = forkIdAt(timestampSeconds);
        const auto& cfg = m_schedule->configAt(timestampSeconds);
        // Defense-in-depth: production karstConfig() is always EVMC_OSAKA, so this arm is
        // unreachable unless OpForkSchedule::configAt is miswired. Kept so Engine can map
        // InconsistentExecutionConfig → -32603 instead of silently serving a wrong rev.
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

    /// Tier-2 attribute-driven build: the mandatory L1-attributes deposit envelope (OP blocks
    /// hard-require a leading deposit). Phase-A field values are zeros (fixture chain without
    /// an L1): Isthmus shape = 176 zero bytes; Jovian/Karst = selector(0x3db6be2b) + zeros to 178.
    /// The deposit's execution against the Ecotone-era genesis L1Block reverts and is
    /// tolerated (documented predeploy-matrix divergence).
    static bcos::bytes synthesizeL1AttributesEnvelope(bcos::engine::OpForkId forkId)
    {
        namespace op = bcos::evm::opstack;
        evmc::bytes data(op::IsthmusL1AttributesLen, 0);
        if (forkId != bcos::engine::OpForkId::Isthmus)
        {
            data.resize(op::JovianL1AttributesLen, 0);
            std::copy(op::JovianL1AttributesSelector.begin(), op::JovianL1AttributesSelector.end(),
                data.begin());
        }
        op::DepositTx deposit{.source_hash = evmc::bytes32{},
            .from = op::OP_DEPOSITOR,
            .to = op::OP_L1_BLOCK,
            .mint = std::nullopt,
            .value = intx::uint256{0},
            .gas_limit = 1'000'000,
            .is_system_tx = false,
            .data = std::move(data)};
        return encodeDepositEnvelope(deposit);
    }

    OpSchedulerSeam(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam(OpSchedulerSeam&&) = delete;
    OpSchedulerSeam& operator=(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam& operator=(OpSchedulerSeam&&) = delete;
    ~OpSchedulerSeam() = default;

    /// Concept-check only — OP mode never calls this. Throws before any co_await/co_return: safe
    /// because the Task coroutine body does not run until the coroutine is actually resumed.
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
};

}  // namespace bcos::evm::engine
