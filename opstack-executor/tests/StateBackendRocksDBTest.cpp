// StateBackendRocksDBTest.cpp — RocksDB state backend round-trip.
//
// Covers the live-chain state path: sidecar import (5 accounts incl. a 2-slot
// contract), the three StateView reads, applyDiff persistence (reopen and re-read),
// and the (height, stateRoot) anchor round-trip. The stateRoot-of-imported-state
// check (--check-import-root) is exercised by the CLI; this unit test pins the
// backend write/read contracts.
#include <boost/test/unit_test.hpp>
#include <bcos-crypto/hash/Keccak256.h>
#include <evmc/hex.hpp>
#include <filesystem>
#include <fstream>

#include "support/StateBackendRocksDB.h"

using namespace op_replay;

namespace fs = std::filesystem;

namespace
{
std::string tempDir()
{
    const auto dir =
        fs::temp_directory_path() / ("op-replay-rocks-" + std::to_string(::getpid()) + "-" +
                                     std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir.string();
}

const std::string kAddrA = "0x00000000000000000000000000000000000000a1";
const std::string kAddrB = "0x00000000000000000000000000000000000000b2";
const std::string kAddrC = "0x00000000000000000000000000000000000000c3";
const std::string kAddrD = "0x00000000000000000000000000000000000000d4";
const std::string kAddrE = "0x00000000000000000000000000000000000000e5";
const std::string kRoot = "0x1111111111111111111111111111111111111111111111111111111111111111";
}  // namespace

BOOST_AUTO_TEST_SUITE(OpStateBackendRocksDB)

BOOST_AUTO_TEST_CASE(ImportReadApplyDiffAnchorRoundtrip)
{
    // 1) Sidecar: 5 accounts. C is a contract with 2 storage slots; B has code;
    //    A/D/E are EOA-ish.
    const fs::path sidecar = fs::temp_directory_path() / "op-replay-sidecar-test.txt";
    {
        std::ofstream out(sidecar);
        out << "MAGIC v1\n";
        out << "ROOT " << kRoot << "\n";
        out << kAddrA << " 0x1 0x0 - 0\n";
        out << kAddrB << " 0x2 0x1 0x6001 0\n";
        out << kAddrC << " 0x3 0x2 0x600260015500 2 "
            << "0x0 0x5 0x1 0x7\n";
        out << kAddrD << " 0x4 0x3 - 0\n";
        out << kAddrE << " 0x5 0x4 - 0\n";
    }

    const auto dir = tempDir();
    // Addresses/slots shared across the write scope and the reopen scope.
    const auto aAddr = test::from_json<evmc::address>(Json::Value(kAddrA));
    const auto bAddr = test::from_json<evmc::address>(Json::Value(kAddrB));
    const auto cAddr = test::from_json<evmc::address>(Json::Value(kAddrC));
    const auto dAddr = test::from_json<evmc::address>(Json::Value(kAddrD));
    const auto slot0 = intx::be::store<evmc::bytes32>(intx::uint256{0});
    const auto slot1 = intx::be::store<evmc::bytes32>(intx::uint256{1});
    const auto slot2 = intx::be::store<evmc::bytes32>(intx::uint256{2});
    {
        StateBackendRocksDB backend(dir);
        backend.loadDumpSidecar(sidecar.string());

        // importRoot parse.
        BOOST_CHECK_EQUAL(hexHash(backend.importRoot()), kRoot);

        // StateView reads.
        const auto a = backend.get_account(aAddr);
        BOOST_REQUIRE(a.has_value());
        BOOST_CHECK_EQUAL(a->nonce, 0u);
        BOOST_CHECK((a->balance == intx::uint256{1}));
        BOOST_CHECK(!a->has_storage);

        const auto c = backend.get_account(cAddr);
        BOOST_REQUIRE(c.has_value());
        BOOST_CHECK(c->has_storage);  // 2 storage rows
        BOOST_CHECK((c->code_hash ==
            evmone::keccak256(evmone::bytes{0x60, 0x02, 0x60, 0x01, 0x55, 0x00})));

        BOOST_CHECK((backend.get_account_code(bAddr) == evmone::bytes{0x60, 0x01}));
        BOOST_CHECK((backend.get_storage(cAddr, slot0) ==
            intx::be::store<evmc::bytes32>(intx::uint256{5})));
        BOOST_CHECK((backend.get_storage(cAddr, slot1) ==
            intx::be::store<evmc::bytes32>(intx::uint256{7})));

        // Full storage map of C.
        const auto storage = backend.getAccountStorage(cAddr);
        BOOST_CHECK_EQUAL(storage.size(), 2u);

        // visitAccounts enumerates all 5.
        size_t seen = 0;
        backend.visitAccounts([&](const AccountView&) {
            ++seen;
            return true;
        });
        BOOST_CHECK_EQUAL(seen, 5u);

        // 2) applyDiff: bump C (balance+nonce, slot1 cleared -> delete, slot2 added),
        //    delete D, replace B's code.
        evmone::state::StateDiff diff;
        evmone::state::StateDiff::Entry entry;
        entry.addr = cAddr;
        entry.nonce = 9;
        entry.balance = intx::uint256{99};
        entry.code = std::nullopt;  // unchanged
        entry.modified_storage = {{slot1, evmc::bytes32{}},  // slot 1 -> 0: delete
            {slot2, intx::be::store<evmc::bytes32>(intx::uint256{11})}};  // slot 2 -> 11
        diff.modified_accounts.push_back(entry);
        diff.deleted_accounts.push_back(dAddr);
        evmone::state::StateDiff::Entry codeEntry;
        codeEntry.addr = bAddr;
        codeEntry.nonce = 1;
        codeEntry.balance = intx::uint256{2};
        codeEntry.code = evmone::bytes{0x60, 0xaa};
        diff.modified_accounts.push_back(codeEntry);
        backend.applyDiff(diff);

        backend.writeAnchor(12345, test::from_json<evmc::bytes32>(Json::Value(kRoot)));
    }  // backend destroyed here: rocksdb holds the DB lock per-process, so the reopen
       // below must run in a fresh scope (simulating a fresh process).

    {
        // 3) Reopen: persistence across DB instances.
        StateBackendRocksDB reopened(dir);
        const auto cAfter = reopened.get_account(cAddr);
        BOOST_REQUIRE(cAfter.has_value());
        BOOST_CHECK_EQUAL(cAfter->nonce, 9u);
        BOOST_CHECK((cAfter->balance == intx::uint256{99}));
        BOOST_CHECK(reopened.get_storage(cAddr, slot1) == evmc::bytes32{});  // deleted
        BOOST_CHECK((reopened.get_storage(
                        cAddr, intx::be::store<evmc::bytes32>(intx::uint256{2})) ==
            intx::be::store<evmc::bytes32>(intx::uint256{11})));
        BOOST_CHECK(!reopened.get_account(dAddr).has_value());  // deleted account
        BOOST_CHECK((reopened.get_account_code(bAddr) == evmone::bytes{0x60, 0xaa}));  // replaced

        // 4) Anchor round-trip.
        const auto anchor = reopened.readAnchor();
        BOOST_REQUIRE(anchor.has_value());
        BOOST_CHECK_EQUAL(anchor->first, 12345u);
        BOOST_CHECK((anchor->second == test::from_json<evmc::bytes32>(Json::Value(kRoot))));

        // Visit after reopen: 4 accounts (D deleted).
        size_t seen2 = 0;
        reopened.visitAccounts([&](const AccountView&) {
            ++seen2;
            return true;
        });
        BOOST_CHECK_EQUAL(seen2, 4u);
    }

    fs::remove_all(dir);
    fs::remove(sidecar);
}

BOOST_AUTO_TEST_SUITE_END()
