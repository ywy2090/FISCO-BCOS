// StateBackendRocksDB.cpp — see StateBackendRocksDB.h.
#include "StateBackendRocksDB.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <evmc/hex.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace op_replay
{
namespace
{
constexpr std::string_view kAccountPrefix = "a/";
constexpr std::string_view kCodePrefix = "c/";
constexpr std::string_view kStoragePrefix = "s/";
constexpr std::string_view kMetaImportRoot = "meta/import_root";
constexpr std::string_view kAnchorState = "anchor/state";

std::string addrKey(const evmc::address& addr, std::string_view prefix)
{
    std::string key(prefix);
    key.append(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes));
    return key;
}

std::string slotKey(const evmc::address& addr, const evmc::bytes32& key)
{
    std::string k(kStoragePrefix);
    k.append(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes));
    k.append(reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
    return k;
}

uint64_t loadBE64(const char* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | static_cast<uint8_t>(p[i]);
    return v;
}

intx::uint256 parseHexU256(const std::string& s)
{
    return intx::from_string<intx::uint256>(s);
}
}  // namespace

StateBackendRocksDB::StateBackendRocksDB(const std::string& rocksPath)
{
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::DB* db = nullptr;
    const auto status = rocksdb::DB::Open(options, rocksPath, &db);
    if (!status.ok())
        throw std::runtime_error("StateBackendRocksDB: open failed at " + rocksPath + ": " +
                                 status.ToString());
    m_db.reset(db);
}

std::optional<evmone::state::StateView::Account> StateBackendRocksDB::get_account(
    const evmc::address& addr) const noexcept
{
    try
    {
        std::string value;
        const auto status =
            m_db->Get(rocksdb::ReadOptions{}, addrKey(addr, kAccountPrefix), &value);
        if (!status.ok() || value.size() != 8 + 32 + 32)
            return std::nullopt;
        evmone::state::StateView::Account acc;
        acc.nonce = loadBE64(value.data());
        acc.balance = intx::be::unsafe::load<intx::uint256>(
            reinterpret_cast<const uint8_t*>(value.data() + 8));
        std::memcpy(acc.code_hash.bytes, value.data() + 40, 32);
        // has_storage semantics (Storage2State::probeHasStorage / TestState::get_account):
        // the account has >=1 non-zero storage slot. Zero-value slots are never persisted
        // (applyDiff erases them), so a prefix presence probe == non-empty storage.
        rocksdb::Iterator* it = m_db->NewIterator(rocksdb::ReadOptions{});
        const auto prefix = addrKey(addr, kStoragePrefix);
        it->Seek(prefix);
        acc.has_storage = it->Valid() && it->key().starts_with(prefix);
        delete it;
        return acc;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

evmone::bytes StateBackendRocksDB::get_account_code(const evmc::address& addr) const noexcept
{
    try
    {
        std::string value;
        const auto status =
            m_db->Get(rocksdb::ReadOptions{}, addrKey(addr, kCodePrefix), &value);
        if (!status.ok())
            return {};
        return evmone::bytes(value.begin(), value.end());
    }
    catch (...)
    {
        return {};
    }
}

evmc::bytes32 StateBackendRocksDB::get_storage(
    const evmc::address& addr, const evmc::bytes32& key) const noexcept
{
    try
    {
        std::string value;
        const auto status = m_db->Get(rocksdb::ReadOptions{}, slotKey(addr, key), &value);
        if (!status.ok() || value.size() != 32)
            return evmc::bytes32{};
        evmc::bytes32 out;
        std::memcpy(out.bytes, value.data(), 32);
        return out;
    }
    catch (...)
    {
        return evmc::bytes32{};
    }
}

void StateBackendRocksDB::applyDiff(const evmone::state::StateDiff& d)
{
    rocksdb::WriteBatch batch;
    for (const auto& m : d.modified_accounts)
    {
        // Full new nonce/balance from the diff entry (evmone StateDiff semantics).
        auto codeHash = evmone::keccak256(get_account_code(m.addr));  // default: unchanged
        if (m.code.has_value())
        {
            codeHash = evmone::keccak256(*m.code);
            if (m.code->empty())
                batch.Delete(addrKey(m.addr, kCodePrefix));
            else
                batch.Put(addrKey(m.addr, kCodePrefix),
                    rocksdb::Slice(reinterpret_cast<const char*>(m.code->data()), m.code->size()));
        }
        writeAccountRow(batch, m.addr, m.nonce, m.balance, codeHash);
        for (const auto& [k, val] : m.modified_storage)
        {
            if (evmc::is_zero(val))
                batch.Delete(slotKey(m.addr, k));
            else
                batch.Put(slotKey(m.addr, k), rocksdb::Slice(
                                                  reinterpret_cast<const char*>(val.bytes), 32));
        }
    }
    for (const auto& addr : d.deleted_accounts)
    {
        batch.Delete(addrKey(addr, kAccountPrefix));
        batch.Delete(addrKey(addr, kCodePrefix));
        // Erase the account's storage rows: prefix scan + delete each key.
        const auto prefix = addrKey(addr, kStoragePrefix);
        rocksdb::Iterator* it = m_db->NewIterator(rocksdb::ReadOptions{});
        for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next())
            batch.Delete(it->key());
        delete it;
    }
    const auto status = m_db->Write(rocksdb::WriteOptions{}, &batch);
    if (!status.ok())
        throw std::runtime_error("StateBackendRocksDB::applyDiff: " + status.ToString());
}

bool StateBackendRocksDB::visitAccounts(
    const std::function<bool(const AccountView&)>& visitor) const
{
    rocksdb::Iterator* it = m_db->NewIterator(rocksdb::ReadOptions{});
    it->Seek(rocksdb::Slice(kAccountPrefix.data(), kAccountPrefix.size()));
    bool ok = true;
    while (it->Valid() && it->key().starts_with(rocksdb::Slice(kAccountPrefix)))
    {
        const auto& key = it->key();
        const auto& value = it->value();
        if (key.size() != kAccountPrefix.size() + sizeof(evmc::address) ||
            value.size() != 8 + 32 + 32)
        {
            it->Next();
            continue;  // malformed row: skip (import-root self-check catches corruption)
        }
        evmc::address addr;
        std::memcpy(addr.bytes, key.data() + kAccountPrefix.size(), sizeof(addr.bytes));
        const uint64_t nonce = loadBE64(value.data());
        const intx::uint256 balance = intx::be::unsafe::load<intx::uint256>(
            reinterpret_cast<const uint8_t*>(value.data() + 8));
        evmc::bytes32 codeHash;
        std::memcpy(codeHash.bytes, value.data() + 40, 32);
        const auto storage = readStoragePrefix(addrKey(addr, kStoragePrefix));
        const AccountView view{addr, nonce, balance, codeHash, storage};
        if (!visitor(view))
        {
            ok = false;
            break;
        }
        it->Next();
    }
    delete it;
    return ok;
}

std::map<evmc::bytes32, evmc::bytes32> StateBackendRocksDB::getAccountStorage(
    const evmc::address& addr) const
{
    return readStoragePrefix(addrKey(addr, kStoragePrefix));
}

void StateBackendRocksDB::loadPre(const JsonValue&)
{
    throw std::runtime_error(
        "StateBackendRocksDB: loadPre unsupported (live path loads state via sidecar)");
}

void StateBackendRocksDB::loadDumpSidecar(const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
        throw std::runtime_error("StateBackendRocksDB: sidecar missing: " + path);
    std::string line;
    // Line 1: MAGIC v1 (version pin — a format drift must fail loudly, not misparse).
    if (!std::getline(input, line) || line != "MAGIC v1")
        throw std::runtime_error("StateBackendRocksDB: sidecar missing 'MAGIC v1' header");
    // Line 2: ROOT 0x<stateRoot>
    if (!std::getline(input, line) || line.rfind("ROOT ", 0) != 0)
        throw std::runtime_error("StateBackendRocksDB: sidecar missing 'ROOT' line");
    const auto rootHex = line.substr(5);
    if (rootHex.size() != 2 + 64)
        throw std::runtime_error("StateBackendRocksDB: malformed ROOT: " + rootHex);
    const auto rootBytes = evmc::from_hex<evmc::bytes32>(rootHex);
    if (!rootBytes.has_value())
        throw std::runtime_error("StateBackendRocksDB: malformed ROOT hex: " + rootHex);
    m_importRoot = *rootBytes;

    rocksdb::WriteBatch batch;
    uint64_t accounts = 0;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        std::istringstream row(line);
        std::string addrHex, balanceHex, nonceHex, codeHex;
        uint64_t storageCount = 0;
        if (!(row >> addrHex >> balanceHex >> nonceHex >> codeHex >> storageCount))
            throw std::runtime_error("StateBackendRocksDB: malformed account line: " + line);
        const auto addr = parseAddr(addrHex);
        if (!addr.has_value())
            throw std::runtime_error("StateBackendRocksDB: malformed addr: " + addrHex);
        const intx::uint256 balance = parseHexU256(balanceHex);
        const uint64_t nonce = static_cast<uint64_t>(
            intx::from_string<intx::uint256>(nonceHex));  // hex w/ optional 0x
        evmone::bytes code;
        if (codeHex != "-")
        {
            const auto codeBytes = evmc::from_hex(codeHex);
            if (!codeBytes.has_value())
                throw std::runtime_error("StateBackendRocksDB: malformed codeHex: " + codeHex);
            code = *codeBytes;
        }
        const auto codeHash = evmone::keccak256(code);
        writeAccountRow(batch, *addr, nonce, balance, codeHash);
        if (!code.empty())
            batch.Put(addrKey(*addr, kCodePrefix),
                rocksdb::Slice(reinterpret_cast<const char*>(code.data()), code.size()));
        for (uint64_t i = 0; i < storageCount; ++i)
        {
            std::string slotHex, valHex;
            if (!(row >> slotHex >> valHex))
                throw std::runtime_error(
                    "StateBackendRocksDB: short storage list on line: " + line);
            const auto slot = intx::be::store<evmc::bytes32>(parseHexU256(slotHex));
            const auto val = intx::be::store<evmc::bytes32>(parseHexU256(valHex));
            if (!evmc::is_zero(val))
                batch.Put(slotKey(*addr, slot),
                    rocksdb::Slice(reinterpret_cast<const char*>(val.bytes), 32));
        }
        ++accounts;
    }
    batch.Put(rocksdb::Slice(kMetaImportRoot), rootHex);
    const auto status = m_db->Write(rocksdb::WriteOptions{}, &batch);
    if (!status.ok())
        throw std::runtime_error("StateBackendRocksDB::loadDumpSidecar: " + status.ToString());
    if (accounts == 0)
        throw std::runtime_error("StateBackendRocksDB: sidecar imported 0 accounts");
}

void StateBackendRocksDB::writeAnchor(uint64_t height, const evmone::hash256& stateRoot)
{
    const auto rootHex = hexHash(stateRoot);
    const auto value = std::to_string(height) + " " + rootHex;
    const auto status = m_db->Put(rocksdb::WriteOptions{}, rocksdb::Slice(kAnchorState), value);
    if (!status.ok())
        throw std::runtime_error("StateBackendRocksDB::writeAnchor: " + status.ToString());
}

std::optional<std::pair<uint64_t, evmone::hash256>> StateBackendRocksDB::readAnchor() const
{
    std::string value;
    const auto status = m_db->Get(rocksdb::ReadOptions{}, rocksdb::Slice(kAnchorState), &value);
    if (!status.ok())
        return std::nullopt;
    std::istringstream row(value);
    uint64_t height = 0;
    std::string rootHex;
    if (!(row >> height >> rootHex))
        return std::nullopt;
    const auto root = evmc::from_hex<evmc::bytes32>(rootHex);
    if (!root.has_value())
        return std::nullopt;
    return std::make_pair(height, *root);
}

std::map<evmc::bytes32, evmc::bytes32> StateBackendRocksDB::readStoragePrefix(
    const std::string& prefix) const
{
    std::map<evmc::bytes32, evmc::bytes32> out;
    rocksdb::Iterator* it = m_db->NewIterator(rocksdb::ReadOptions{});
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next())
    {
        const auto& key = it->key();
        const auto& value = it->value();
        if (key.size() != prefix.size() + 32 || value.size() != 32)
            continue;
        evmc::bytes32 k, v;
        std::memcpy(k.bytes, key.data() + prefix.size(), 32);
        std::memcpy(v.bytes, value.data(), 32);
        if (!evmc::is_zero(v))
            out[k] = v;
    }
    delete it;
    return out;
}

void StateBackendRocksDB::writeAccountRow(rocksdb::WriteBatch& batch, const evmc::address& addr,
    uint64_t nonce, const intx::uint256& balance, const evmc::bytes32& codeHash)
{
    std::string value;
    value.resize(8 + 32 + 32);
    for (int i = 0; i < 8; ++i)
        value[7 - i] = static_cast<char>((nonce >> (8 * i)) & 0xff);
    const auto balanceBe = intx::be::store<evmc::uint256be>(balance);
    std::memcpy(value.data() + 8, balanceBe.bytes, 32);
    std::memcpy(value.data() + 40, codeHash.bytes, 32);
    batch.Put(addrKey(addr, kAccountPrefix), value);
}

std::optional<evmc::address> StateBackendRocksDB::parseAddr(const std::string& s)
{
    const auto bytes = evmc::from_hex<evmc::address>(s);
    if (!bytes.has_value())
        return std::nullopt;
    return *bytes;
}
}  // namespace op_replay
