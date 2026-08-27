#pragma once

#include "OpForkScheduleCodec.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger
{
inline constexpr std::string_view OP_FORK_SCHEDULE_KEY = "op_fork_schedule";
inline constexpr std::string_view OP_FORK_SCHEDULE_HASH_KEY = "op_fork_schedule_hash";
inline constexpr std::string_view OP_FORK_SCHEDULE_GENESIS_KEY = "op_fork_schedule_genesis";

struct OpForkScheduleMetadata
{
    std::string schedule;
    crypto::HashType scheduleHash;
    crypto::HashType genesisHash;
};

struct OpForkScheduleRuntime
{
    std::string canonical;
    bool legacyMemoryOnly = false;
};

struct OpForkScheduleMetadataRows
{
    std::optional<std::string> schedule;
    std::optional<std::string> scheduleHash;
    std::optional<std::string> genesisHash;
};

[[nodiscard]] inline bool opForkScheduleMetadataRowsAbsent(
    OpForkScheduleMetadataRows const& rows) noexcept
{
    return !rows.schedule.has_value() && !rows.scheduleHash.has_value() &&
           !rows.genesisHash.has_value();
}

[[nodiscard]] inline bool opForkScheduleMetadataRowsPartial(
    OpForkScheduleMetadataRows const& rows) noexcept
{
    const bool any =
        rows.schedule.has_value() || rows.scheduleHash.has_value() || rows.genesisHash.has_value();
    const bool all =
        rows.schedule.has_value() && rows.scheduleHash.has_value() && rows.genesisHash.has_value();
    return any && !all;
}

[[nodiscard]] inline bool opForkScheduleRequestsKarst(std::string_view canonical)
{
    const auto activations = parseOpForkSchedule(canonical);
    for (const auto& activation : activations)
    {
        if (activation.forkName == "karst")
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool opForkScheduleHasDaFootprintAt(
    std::string_view canonical, uint64_t timestampSeconds)
{
    const auto activations = parseOpForkSchedule(canonical);
    if (activations.empty() || timestampSeconds < activations.front().timestamp)
    {
        return false;
    }
    std::string activeFork = activations.front().forkName;
    for (const auto& activation : activations)
    {
        if (activation.timestamp > timestampSeconds)
        {
            break;
        }
        activeFork = activation.forkName;
    }
    return activeFork == "jovian" || activeFork == "karst";
}

[[nodiscard]] inline std::string canonicalizeIniOpForkSchedule(std::string_view raw)
{
    return canonicalOpForkSchedule(parseOpForkSchedule(raw));
}

[[nodiscard]] inline OpForkScheduleMetadata buildOpForkScheduleMetadata(
    std::string canonical, crypto::HashType genesisHash)
{
    const auto scheduleHash = keccakOpForkScheduleHash(canonical);
    return OpForkScheduleMetadata{
        .schedule = std::move(canonical),
        .scheduleHash = scheduleHash,
        .genesisHash = std::move(genesisHash),
    };
}

[[nodiscard]] inline OpForkScheduleMetadata validateOpForkScheduleMetadataRows(
    OpForkScheduleMetadataRows rows, crypto::HashType const& expectedGenesisHash)
{
    if (opForkScheduleMetadataRowsAbsent(rows))
    {
        throw InvalidOpForkSchedule("op fork schedule metadata is absent");
    }
    if (opForkScheduleMetadataRowsPartial(rows))
    {
        throw InvalidOpForkSchedule("partial op fork schedule metadata triple");
    }

    auto metadata =
        buildOpForkScheduleMetadata(*rows.schedule, crypto::HashType{*rows.genesisHash});
    if (metadata.genesisHash != expectedGenesisHash)
    {
        throw InvalidOpForkSchedule("op fork schedule genesis binding mismatch");
    }
    if (metadata.scheduleHash != crypto::HashType{*rows.scheduleHash})
    {
        throw InvalidOpForkSchedule("op fork schedule hash mismatch");
    }
    return metadata;
}

namespace detail
{
inline executor_v1::StateKeyView opForkScheduleMetadataKey(std::string_view key)
{
    return executor_v1::StateKeyView(SYS_CHAIN_METADATA, key);
}
}  // namespace detail

template <class Storage>
task::Task<OpForkScheduleMetadataRows> readOpForkScheduleMetadataRows(Storage& storage)
{
    const auto entries = co_await storage2::readSome(
        storage, std::array{detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_KEY),
                     detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_HASH_KEY),
                     detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_GENESIS_KEY)});

    OpForkScheduleMetadataRows rows;
    if (entries[0])
    {
        rows.schedule = entries[0]->get();
    }
    if (entries[1])
    {
        rows.scheduleHash = entries[1]->get();
    }
    if (entries[2])
    {
        rows.genesisHash = entries[2]->get();
    }
    co_return rows;
}

template <class Storage>
task::Task<std::optional<OpForkScheduleMetadata>> readOpForkScheduleMetadata(
    Storage& storage, crypto::HashType const& expectedGenesisHash)
{
    auto rows = co_await readOpForkScheduleMetadataRows(storage);
    if (opForkScheduleMetadataRowsAbsent(rows))
    {
        co_return std::nullopt;
    }
    co_return validateOpForkScheduleMetadataRows(std::move(rows), expectedGenesisHash);
}

template <class Storage>
task::Task<void> writeOpForkScheduleMetadata(
    Storage& storage, OpForkScheduleMetadata const& metadata)
{
    storage::Entry scheduleEntry;
    scheduleEntry.set(metadata.schedule);
    co_await storage2::writeOne(
        storage, detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_KEY), std::move(scheduleEntry));

    storage::Entry hashEntry;
    hashEntry.set(metadata.scheduleHash.hex());
    co_await storage2::writeOne(storage,
        detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_HASH_KEY), std::move(hashEntry));

    storage::Entry genesisEntry;
    genesisEntry.set(metadata.genesisHash.hex());
    co_await storage2::writeOne(storage,
        detail::opForkScheduleMetadataKey(OP_FORK_SCHEDULE_GENESIS_KEY), std::move(genesisEntry));
}
}  // namespace bcos::ledger
