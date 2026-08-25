/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-framework/ledger/ChainMetadata.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/OpForkScheduleMigration.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-storage/RocksDBStorage.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/bcos-tool/BfsFileFactory.h"
#include "libinitializer/StorageInitializer.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::initializer;
using namespace bcos::storage;

namespace
{
constexpr size_t kHardcodedKeyPageSize = 10240;
constexpr auto kHardcodedBlockVersion = protocol::BlockVersion::V3_18_0_VERSION;

void printToolLimitations(std::ostream& out)
{
    out << "\nLimitations:\n"
        << "  - KeyPage size is hardcoded to " << kHardcodedKeyPageSize
        << " (config.ini is not read)\n"
        << "  - Block version is hardcoded to V3_18_0\n"
        << "  - Encrypted RocksDB storage is not supported\n";
}

void printUsage(std::ostream& out, po::options_description const& options)
{
    out << options << '\n';
    printToolLimitations(out);
}

std::shared_ptr<std::set<std::string, std::less<>>> defaultKeyPageIgnoreTables()
{
    return std::make_shared<std::set<std::string, std::less<>>>(
        std::initializer_list<std::set<std::string, std::less<>>::value_type>{
            std::string(ledger::SYS_CONFIG),
            std::string(ledger::SYS_CONSENSUS),
            std::string(ledger::SYS_CURRENT_STATE),
            std::string(ledger::SYS_HASH_2_NUMBER),
            std::string(ledger::SYS_NUMBER_2_HASH),
            std::string(ledger::SYS_BLOCK_NUMBER_2_NONCES),
            std::string(ledger::SYS_NUMBER_2_BLOCK_HEADER),
            std::string(ledger::SYS_NUMBER_2_TXS),
            std::string(ledger::SYS_HASH_2_TX),
            std::string(ledger::SYS_HASH_2_RECEIPT),
            std::string(storage::FS_ROOT),
            std::string(storage::FS_APPS),
            std::string(storage::FS_USER),
            std::string(storage::FS_SYS_BIN),
            std::string(storage::FS_USER_TABLE),
            std::string(ledger::SYS_CONTRACT_ABI),
            std::string(ledger::SYS_CODE_BINARY),
            storage::StorageInterface::SYS_TABLES,
        });
}

StateStorageInterface::Ptr createKeyPageStorage(
    StorageInterface::Ptr backend, size_t keyPageSize, uint32_t blockVersion)
{
    auto ignoreTables = defaultKeyPageIgnoreTables();
    if (blockVersion >= static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION))
    {
        for (const auto& sub : tool::FS_ROOT_SUBS)
        {
            ignoreTables->erase(std::string(sub));
        }
    }
    return std::make_shared<KeyPageStorage>(
        backend, false, keyPageSize, blockVersion, ignoreTables);
}

BlockFactory::Ptr createDefaultBlockFactory()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

TransactionalStorageInterface::Ptr openWritableRocksDB(const std::string& storagePath)
{
    RocksDBOption option;
    auto rocksdb = StorageInitializer::createRocksDB(storagePath, option);
    bcos::security::StorageEncryptInterface::Ptr dataEncryption;
    return StorageInitializer::build(std::move(rocksdb), dataEncryption);
}

void commitKeyPageChanges(
    TransactionalStorageInterface::Ptr rocksdbStorage, StorageInterface::Ptr layeredStorage)
{
    auto* keyPageStorage = dynamic_cast<TraverseStorageInterface*>(layeredStorage.get());
    if (keyPageStorage == nullptr)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "keypage storage backend unavailable; metadata write was not committed"));
    }
    bcos::protocol::TwoPCParams params;
    std::promise<Error::Ptr> preparePromise;
    rocksdbStorage->asyncPrepare(
        params, *keyPageStorage, [&](Error::Ptr err, uint64_t, const std::string&) {
            preparePromise.set_value(std::move(err));
        });
    auto prepareErr = preparePromise.get_future().get();
    if (prepareErr)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("asyncPrepare failed: " + prepareErr->errorMessage()));
    }

    std::promise<Error::Ptr> commitPromise;
    rocksdbStorage->asyncCommit(
        params, [&](Error::Ptr err, uint64_t) { commitPromise.set_value(std::move(err)); });
    auto commitErr = commitPromise.get_future().get();
    if (commitErr)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("asyncCommit failed: " + commitErr->errorMessage()));
    }
}

void printMigrationSummary(HashType const& genesisHash, uint64_t safeHeadTimestampSeconds,
    std::string const& oldCanonical, HashType const& oldScheduleHash,
    std::string const& newCanonical, HashType const& newScheduleHash)
{
    std::cout << "genesis_hash=" << genesisHash.hex() << '\n'
              << "safe_head_timestamp_seconds=" << safeHeadTimestampSeconds << '\n'
              << "old_schedule=" << oldCanonical << '\n'
              << "old_schedule_hash=" << oldScheduleHash.hex() << '\n'
              << "new_schedule=" << newCanonical << '\n'
              << "new_schedule_hash=" << newScheduleHash.hex() << '\n';
}

int runMigration(
    const std::string& storagePath, const std::string& rawSchedule, bool dryRun, bool yes)
{
    if (!fs::exists(storagePath))
    {
        std::cerr << "storage path does not exist: " << storagePath << '\n';
        printToolLimitations(std::cerr);
        return 1;
    }

    auto rocksdbStorage = openWritableRocksDB(storagePath);
    auto layeredStorage = createKeyPageStorage(
        rocksdbStorage, kHardcodedKeyPageSize, static_cast<uint32_t>(kHardcodedBlockVersion));

    auto blockFactory = createDefaultBlockFactory();
    auto ledger = std::make_shared<Ledger>(blockFactory, layeredStorage, 1);

    const std::string newCanonical = canonicalOpForkSchedule(parseOpForkSchedule(rawSchedule));

    try
    {
        task::syncWait([&]() -> task::Task<void> {
            const auto blockNumber =
                co_await getCurrentBlockNumber(*layeredStorage, ledger::FromStorage{});
            if (blockNumber < 0)
            {
                throw std::runtime_error("current block number unavailable");
            }

            auto genesisBlock = co_await getBlockData(*ledger, 0, HEADER);
            if (!genesisBlock || !genesisBlock->blockHeader())
            {
                throw std::runtime_error("genesis block header unavailable");
            }
            const auto genesisHash = genesisBlock->blockHeader()->hash();

            auto safeBlock = co_await getBlockData(*ledger, blockNumber, HEADER);
            if (!safeBlock || !safeBlock->blockHeader())
            {
                throw std::runtime_error("safe head block header unavailable");
            }
            const auto safeHeadTimestampSeconds =
                opForkScheduleSafeHeadSeconds(safeBlock->blockHeader()->timestamp());

            const auto oldMetadata =
                co_await readOpForkScheduleMetadata(*layeredStorage, genesisHash);
            if (!oldMetadata.has_value())
            {
                throw InvalidOpForkSchedule("op fork schedule metadata is absent");
            }

            validateOpForkScheduleMigration(
                oldMetadata->schedule, newCanonical, safeHeadTimestampSeconds);

            const auto newMetadata = buildOpForkScheduleMetadata(newCanonical, genesisHash);
            printMigrationSummary(genesisHash, safeHeadTimestampSeconds, oldMetadata->schedule,
                oldMetadata->scheduleHash, newMetadata.schedule, newMetadata.scheduleHash);

            if (oldMetadata->schedule == newMetadata.schedule &&
                oldMetadata->scheduleHash == newMetadata.scheduleHash)
            {
                std::cout << "status=already_migrated\n";
                co_return;
            }

            if (dryRun)
            {
                std::cout << "status=dry_run_ok\n";
                co_return;
            }

            if (!yes)
            {
                throw std::runtime_error("refusing to write without --yes");
            }

            co_await writeOpForkScheduleMetadata(*layeredStorage, newMetadata);
            commitKeyPageChanges(rocksdbStorage, layeredStorage);
            std::cout << "status=migrated\n";
            co_return;
        }());
    }
    catch (const InvalidOpForkScheduleMigration& ex)
    {
        std::cerr << "migration rejected: " << ex.what() << '\n';
        return 2;
    }
    catch (const InvalidOpForkSchedule& ex)
    {
        std::cerr << "invalid schedule: " << ex.what() << '\n';
        return 2;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    po::options_description options("op-fork-schedule-migrate");
    options.add_options()("help,h", "show help")(
        "storage,s", po::value<std::string>(), "RocksDB state storage path")("schedule",
        po::value<std::string>(), "target canonical fork schedule")("dry-run", po::bool_switch(),
        "validate and print without writing")("yes,y", po::bool_switch(), "confirm metadata write");

    po::variables_map params;
    try
    {
        po::store(po::parse_command_line(argc, argv, options), params);
        po::notify(params);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "argument error: " << ex.what() << '\n';
        printUsage(std::cerr, options);
        return 1;
    }

    if (params.count("help") != 0U)
    {
        printUsage(std::cout, options);
        return 0;
    }

    if (!params.count("storage"))
    {
        std::cerr << "missing required --storage\n";
        printUsage(std::cerr, options);
        return 1;
    }
    if (!params.count("schedule"))
    {
        std::cerr << "missing required --schedule\n";
        printUsage(std::cerr, options);
        return 1;
    }

    const bool dryRun = params["dry-run"].as<bool>();
    const bool yes = params["yes"].as<bool>();
    return runMigration(
        params["storage"].as<std::string>(), params["schedule"].as<std::string>(), dryRun, yes);
}
