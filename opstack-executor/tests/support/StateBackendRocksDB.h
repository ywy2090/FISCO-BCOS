// StateBackendRocksDB.h — RocksDB-backed StateBackend for the live-chain replay path.
//
// Implements the op_replay::StateBackend generalization (ReplayGate.h) over a
// raw rocksdb::DB: the StateView reads (get_account/get_account_code/get_storage),
// StateDiff write-back (applyDiff), full account enumeration (visitAccounts,
// the stateRootOf<Ledger> contract), a per-account storage-map read
// (getAccountStorage) and vector-`pre` loading (loadPre, unused on the live path).
//
// State bootstrap comes from a dump sidecar (loadDumpSidecar, format written by
// the Go `--live` generator):
//
//   MAGIC v1
//   ROOT 0x<stateRoot(H0-1)>
//   <addr> <balance> <nonce> <codeHex|-> <storageCount> [<slot> <val>]...
//
// Key layout (raw rocksdb rows, prefix-sorted so visitAccounts can prefix-scan):
//   a/<20B addr>          -> 8B nonce BE | 32B balance BE | 32B codeHash
//   c/<20B addr>          -> account code bytes
//   s/<20B addr>/<32B key> -> 32B slot value BE
//   meta/import_root      -> "0x" + hex stateRoot (from the sidecar ROOT line)
//   anchor/state          -> "<uint64 height> 0x<root>" (checkpoint for resume)
#pragma once

#include "ReplayGate.h"
#include <rocksdb/db.h>
#include <cstdint>
#include <string>

namespace op_replay
{
class StateBackendRocksDB final : public StateBackend
{
public:
    explicit StateBackendRocksDB(const std::string& rocksPath);

    // StateView three pure virtuals (signatures per bcos-evm/eth/state/state_view.hpp).
    // All noexcept: a read failure is treated as absent/zero — import corruption is
    // caught by the --check-import-root self-check instead of terminating here.
    std::optional<evmone::state::StateView::Account> get_account(
        const evmc::address& addr) const noexcept override;
    evmone::bytes get_account_code(const evmc::address& addr) const noexcept override;
    evmc::bytes32 get_storage(const evmc::address& addr, const evmc::bytes32& key) const noexcept override;

    // StateBackend extension points.
    void applyDiff(const evmone::state::StateDiff& d) override;
    bool visitAccounts(const std::function<bool(const AccountView&)>& visitor) const override;
    std::map<evmc::bytes32, evmc::bytes32> getAccountStorage(const evmc::address& addr) const override;
    void loadPre(const JsonValue& pre) override;  // unused on the live path (throws)

    // Sidecar bootstrap + anchors.
    void loadDumpSidecar(const std::string& path);
    evmone::hash256 importRoot() const noexcept { return m_importRoot; }
    void writeAnchor(uint64_t height, const evmone::hash256& stateRoot);
    std::optional<std::pair<uint64_t, evmone::hash256>> readAnchor() const;

private:
    std::map<evmc::bytes32, evmc::bytes32> readStoragePrefix(const std::string& prefix) const;
    void writeAccountRow(rocksdb::WriteBatch& batch, const evmc::address& addr, uint64_t nonce,
        const intx::uint256& balance, const evmc::bytes32& codeHash);
    static std::optional<evmc::address> parseAddr(const std::string& s);

    std::unique_ptr<rocksdb::DB> m_db;
    evmone::hash256 m_importRoot{};  // zero until loadDumpSidecar
};
}  // namespace op_replay
