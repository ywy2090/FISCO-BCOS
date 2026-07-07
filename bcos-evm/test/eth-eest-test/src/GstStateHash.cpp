#include "bcos-evm/eth-eest-test/GstStateHash.h"

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <bcos-crypto/hash/Sha256.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace bcos::evm::reference_tests
{
namespace
{

evmc_bytes32 const EMPTY_ROOT = {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45,
    0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0, 0x01, 0x62, 0x2f,
    0xb5, 0xe3, 0x63, 0xb4, 0x21};

evmc_bytes32 const EMPTY_CODE_HASH = {0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c, 0x92, 0x7e,
    0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0, 0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b, 0x7b, 0xfa,
    0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70};

evmc_bytes32 keccak256(bcos::bytes const& data)
{
    auto const hash = ethash::keccak256(data.data(), data.size());
    evmc_bytes32 out{};
    std::memcpy(out.bytes, hash.bytes, sizeof(out.bytes));
    return out;
}

evmc_bytes32 sha256ToBytes32(bcos::crypto::HashType const& hash)
{
    evmc_bytes32 out{};
    std::memcpy(out.bytes, hash.data(), sizeof(out.bytes));
    return out;
}

void rlpAppendLength(bcos::bytes& out, size_t length, uint8_t offset)
{
    if (length < 56)
    {
        out.push_back(static_cast<uint8_t>(offset + length));
        return;
    }
    bcos::bytes lenBytes;
    while (length > 0)
    {
        lenBytes.insert(lenBytes.begin(), static_cast<uint8_t>(length & 0xff));
        length >>= 8;
    }
    out.push_back(static_cast<uint8_t>(offset + 55 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
}

std::vector<uint8_t> keyToNibbles(bcos::bytes const& key)
{
    std::vector<uint8_t> nibbles;
    nibbles.reserve(key.size() * 2);
    for (auto const byte : key)
    {
        auto const v = static_cast<uint8_t>(byte);
        nibbles.push_back(static_cast<uint8_t>(v >> 4));
        nibbles.push_back(static_cast<uint8_t>(v & 0x0f));
    }
    return nibbles;
}

bcos::bytes encode_path(std::vector<uint8_t> const& nibbles, bool terminating)
{
    bcos::bytes out(nibbles.size() / 2 + 1, 0);
    bool const odd = (nibbles.size() & 1U) != 0;
    out[0] = terminating ? 0x20 : 0x00;
    out[0] |= odd ? 0x10 : 0x00;

    size_t index = 0;
    if (odd)
    {
        out[0] = static_cast<uint8_t>(out[0] | (nibbles[0] & 0x0f));
        index = 1;
    }

    size_t outIndex = 1;
    for (; index + 1 < nibbles.size(); index += 2)
    {
        out[outIndex++] = static_cast<uint8_t>((nibbles[index] << 4) | nibbles[index + 1]);
    }
    return out;
}

struct TrieEntry
{
    bcos::bytes key;
    bcos::bytes value;
};

bcos::bytes wrap_hash(evmc_bytes32 const& hash)
{
    bcos::bytes wrapped(33);
    wrapped[0] = 0xa0;
    std::memcpy(wrapped.data() + 1, hash.bytes, sizeof(hash.bytes));
    return wrapped;
}

bcos::bytes node_ref(bcos::bytes const& encoded)
{
    if (encoded.size() < 32)
    {
        return encoded;
    }
    return wrap_hash(keccak256(encoded));
}

size_t prefix_length(std::vector<uint8_t> const& lhs, std::vector<uint8_t> const& rhs)
{
    size_t len = 0;
    while (len < lhs.size() && len < rhs.size() && lhs[len] == rhs[len])
    {
        ++len;
    }
    return len;
}

class HashBuilder
{
public:
    void add_leaf(std::vector<uint8_t> key, bcos::bytes value)
    {
        if (!key_.empty() && !(key > key_))
        {
            throw std::runtime_error("Trie entries must be strictly increasing");
        }
        if (!key_.empty())
        {
            gen_struct_step(key_, key);
        }
        key_ = std::move(key);
        value_ = std::move(value);
    }

    void finalize()
    {
        if (!key_.empty())
        {
            gen_struct_step(key_, {});
            key_.clear();
            value_ = bcos::bytes{};
        }
    }

    evmc_bytes32 root_hash()
    {
        finalize();
        if (stack_.empty())
        {
            return EMPTY_ROOT;
        }

        auto const& rootRef = stack_.back();
        if (rootRef.size() == 33 && rootRef[0] == 0xa0)
        {
            evmc_bytes32 out{};
            std::memcpy(out.bytes, rootRef.data() + 1, sizeof(out.bytes));
            return out;
        }
        return keccak256(rootRef);
    }

private:
    bcos::bytes leaf_node_rlp(std::vector<uint8_t> const& path, bcos::bytes const& value)
    {
        auto const encodedPath = encode_path(path, true);
        return rlpEncodeList({rlpEncodeRaw(encodedPath), rlpEncodeRaw(value)});
    }

    bcos::bytes extension_node_rlp(std::vector<uint8_t> const& path, bcos::bytes const& childRef)
    {
        auto const encodedPath = encode_path(path, false);
        return rlpEncodeList({rlpEncodeRaw(encodedPath), childRef});
    }

    std::vector<bcos::bytes> branch_ref(uint16_t stateMask, uint16_t hashMask)
    {
        if ((hashMask & ~stateMask) != 0)
        {
            throw std::runtime_error("Invalid branch masks");
        }
        size_t const childCount = static_cast<size_t>(std::popcount(stateMask));
        if (childCount > stack_.size())
        {
            throw std::runtime_error("Invalid stack size for branch");
        }

        size_t const firstChild = stack_.size() - childCount;
        std::vector<bcos::bytes> childHashes;
        childHashes.reserve(static_cast<size_t>(std::popcount(hashMask)));
        std::vector<bcos::bytes> items;
        items.reserve(17);

        size_t childIndex = firstChild;
        for (uint8_t digit = 0; digit < 16; ++digit)
        {
            if ((stateMask & (1u << digit)) != 0)
            {
                if ((hashMask & (1u << digit)) != 0)
                {
                    childHashes.push_back(stack_[childIndex]);
                }
                items.push_back(stack_[childIndex++]);
            }
            else
            {
                items.push_back({0x80});
            }
        }
        items.push_back({0x80});

        auto const branchRlp = rlpEncodeList(items);
        stack_.resize(firstChild + 1);
        stack_.back() = node_ref(branchRlp);
        return childHashes;
    }

    void gen_struct_step(std::vector<uint8_t> current, std::vector<uint8_t> const& succeeding)
    {
        for (bool buildExtensions = false;; buildExtensions = true)
        {
            bool const precedingExists = !groups_.empty();
            size_t const precedingLen = groups_.empty() ? 0 : groups_.size() - 1;
            size_t const commonPrefixLen = prefix_length(succeeding, current);
            size_t const len = std::max(precedingLen, commonPrefixLen);
            if (len >= current.size())
            {
                throw std::runtime_error("Invalid trie key ordering");
            }

            uint8_t const extraDigit = current[len];
            if (groups_.size() <= len)
            {
                groups_.resize(len + 1);
            }
            groups_[len] = static_cast<uint16_t>(groups_[len] | (1u << extraDigit));

            size_t from = len;
            if (!succeeding.empty() || precedingExists)
            {
                ++from;
            }
            std::vector<uint8_t> shortNodeKey(
                current.begin() + static_cast<long>(from), current.end());

            if (!buildExtensions)
            {
                stack_.push_back(
                    node_ref(leaf_node_rlp(shortNodeKey, std::get<bcos::bytes>(value_))));
            }

            if (buildExtensions && !shortNodeKey.empty())
            {
                stack_.back() = node_ref(extension_node_rlp(shortNodeKey, stack_.back()));
            }

            if (precedingLen <= commonPrefixLen && !succeeding.empty())
            {
                return;
            }

            if (!succeeding.empty() || precedingExists)
            {
                (void)branch_ref(groups_[len], groups_[len]);
            }

            groups_.resize(len);
            if (precedingLen == 0)
            {
                return;
            }

            current.resize(precedingLen);
            while (!groups_.empty() && groups_.back() == 0)
            {
                groups_.pop_back();
            }
        }
    }

    std::vector<uint16_t> groups_;
    std::vector<uint8_t> key_;
    std::variant<bcos::bytes> value_;
    std::vector<bcos::bytes> stack_;
};

evmc_bytes32 hashTrieEntries(std::vector<TrieEntry> entries)
{
    if (entries.empty())
    {
        return EMPTY_ROOT;
    }
    std::sort(entries.begin(), entries.end(),
        [](TrieEntry const& lhs, TrieEntry const& rhs) { return lhs.key < rhs.key; });

    HashBuilder builder;
    for (auto const& [key, value] : entries)
    {
        builder.add_leaf(keyToNibbles(key), value);
    }
    return builder.root_hash();
}

evmc_bytes32 hashStorageTrie(state::StorageMap const& storage)
{
    std::vector<TrieEntry> entries;
    entries.reserve(storage.size());
    for (auto const& [slot, value] : storage)
    {
        if (state::isZeroBytes32(value))
        {
            continue;
        }
        bcos::bytes slotKey(slot.bytes, slot.bytes + sizeof(slot.bytes));
        auto const hashedKey = keccak256(slotKey);
        bcos::bytes valueBytes(value.bytes, value.bytes + sizeof(value.bytes));
        while (!valueBytes.empty() && valueBytes.front() == 0)
        {
            valueBytes.erase(valueBytes.begin());
        }
        entries.push_back(TrieEntry{.key = bcos::bytes(hashedKey.bytes, hashedKey.bytes + 32),
            .value = rlpEncodeRaw(valueBytes)});
    }
    return hashTrieEntries(std::move(entries));
}

evmc_bytes32 accountCodeHash(state::Account const& account)
{
    if (account.code.empty())
    {
        return EMPTY_CODE_HASH;
    }
    if (!state::isZeroBytes32(account.codeHash))
    {
        return account.codeHash;
    }
    return keccak256(account.code);
}

bcos::bytes encodeAccount(state::Account const& account, evmc_bytes32 const& storageRoot)
{
    auto const codeHash = accountCodeHash(account);
    return rlpEncodeList({rlpEncodeUint64(account.nonce), rlpEncodeU256(account.balance),
        rlpEncodeRaw(bcos::bytes(storageRoot.bytes, storageRoot.bytes + sizeof(storageRoot.bytes))),
        rlpEncodeRaw(bcos::bytes(codeHash.bytes, codeHash.bytes + sizeof(codeHash.bytes)))});
}

bool isEmptyAccount(state::Account const& account)
{
    return account.nonce == 0 && account.balance == 0 && account.code.empty();
}

evmc_bytes32 computeStateRootImpl(GstPostStateView const& postState)
{
    std::vector<TrieEntry> entries;
    entries.reserve(postState.accounts.size());
    for (auto const& [address, account] : postState.accounts)
    {
        if (postState.eip158ClearEmpty && isEmptyAccount(account))
        {
            continue;
        }
        auto const storageRoot = hashStorageTrie(account.storage);
        auto const encodedAccount = encodeAccount(account, storageRoot);
        bcos::bytes addressBytes(address.bytes, address.bytes + sizeof(address.bytes));
        auto const hashedAddress = keccak256(addressBytes);
        entries.push_back(
            TrieEntry{.key = bcos::bytes(hashedAddress.bytes, hashedAddress.bytes + 32),
                .value = encodedAccount});
    }
    return hashTrieEntries(std::move(entries));
}

bcos::bytes encodeLog(state::LogEntry const& log)
{
    std::vector<bcos::bytes> topics;
    topics.reserve(log.topics.size());
    for (auto const& topic : log.topics)
    {
        topics.push_back(rlpEncodeRaw(bcos::bytes(topic.bytes, topic.bytes + sizeof(topic.bytes))));
    }
    return rlpEncodeList({rlpEncodeRaw(bcos::bytes(log.address.bytes, log.address.bytes + 20)),
        rlpEncodeList(topics), rlpEncodeRaw(log.data)});
}

evmc_bytes32 computeLogsHashImpl(std::vector<state::LogEntry> const& logs)
{
    if (logs.empty())
    {
        return keccak256(rlpEncodeList({}));
    }
    std::vector<bcos::bytes> encodedLogs;
    encodedLogs.reserve(logs.size());
    for (auto const& log : logs)
    {
        encodedLogs.push_back(encodeLog(log));
    }
    return keccak256(rlpEncodeList(encodedLogs));
}

bcos::bytes encodeReceipt(ReceiptForRoot const& receipt)
{
    bcos::bytes statusByte =
        receipt.status == EVMC_SUCCESS ? rlpEncodeUint64(1) : bcos::bytes{0x80};
    std::vector<bcos::bytes> encodedLogs;
    encodedLogs.reserve(receipt.logs.size());
    for (auto const& log : receipt.logs)
    {
        encodedLogs.push_back(encodeLog(log));
    }
    bcos::bytes payload =
        rlpEncodeList({std::move(statusByte), rlpEncodeUint64(receipt.cumulativeGasUsed),
            rlpEncodeRaw(receipt.bloom), rlpEncodeList(encodedLogs)});
    if (receipt.txType != 0)
    {
        bcos::bytes typed;
        typed.push_back(receipt.txType);
        typed.insert(typed.end(), payload.begin(), payload.end());
        return typed;
    }
    return payload;
}

bcos::bytes encodeWithdrawal(Withdrawal const& withdrawal)
{
    bcos::bytes addressBytes(
        withdrawal.address.bytes, withdrawal.address.bytes + sizeof(withdrawal.address.bytes));
    return rlpEncodeList(
        {rlpEncodeUint64(withdrawal.index), rlpEncodeUint64(withdrawal.validatorIndex),
            rlpEncodeRaw(addressBytes), rlpEncodeUint64(withdrawal.amount)});
}

evmc_bytes32 computeIndexedTrieRoot(size_t count, std::function<bcos::bytes(size_t)> encodeEntry)
{
    std::vector<TrieEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        entries.push_back(TrieEntry{.key = rlpEncodeUint64(i), .value = encodeEntry(i)});
    }
    return hashTrieEntries(std::move(entries));
}

}  // namespace

bcos::bytes rlpEncodeRaw(bcos::bytes const& input)
{
    bcos::bytes out;
    if (input.size() == 1 && input[0] < 0x80)
    {
        out.push_back(input[0]);
        return out;
    }
    rlpAppendLength(out, input.size(), 0x80);
    out.insert(out.end(), input.begin(), input.end());
    return out;
}

bcos::bytes rlpEncodeUint64(uint64_t value)
{
    if (value == 0)
    {
        return {0x80};
    }
    bcos::bytes encoded;
    while (value > 0)
    {
        encoded.insert(encoded.begin(), static_cast<uint8_t>(value & 0xff));
        value >>= 8;
    }
    if (encoded.size() == 1 && encoded[0] < 0x80)
    {
        return encoded;
    }
    bcos::bytes out;
    rlpAppendLength(out, encoded.size(), 0x80);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}

bcos::bytes rlpEncodeU256(bcos::u256 value)
{
    if (value == 0)
    {
        return {0x80};
    }
    auto encoded = bcos::toBigEndian(value);
    while (!encoded.empty() && encoded.front() == 0)
    {
        encoded.erase(encoded.begin());
    }
    return rlpEncodeRaw(encoded);
}

bcos::bytes rlpEncodeList(std::vector<bcos::bytes> const& items)
{
    bcos::bytes payload;
    for (auto const& item : items)
    {
        payload.insert(payload.end(), item.begin(), item.end());
    }
    bcos::bytes out;
    rlpAppendLength(out, payload.size(), 0xc0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

GstPostStateView buildPostStateView(
    std::vector<std::pair<evmc_address, state::Account>> const& preState,
    state::StateDiff const& stateDiff, bool applyDiff, evmc_address const& coinbase, bool eip158)
{
    std::unordered_map<evmc_address, state::Account, state::AddressHash, state::AddressEqual>
        accounts;
    for (auto const& [address, account] : preState)
    {
        accounts.emplace(address, account);
    }

    if (applyDiff)
    {
        for (auto const& address : stateDiff.deletedAccounts)
        {
            accounts.erase(address);
        }
        for (auto const& [address, account] : stateDiff.accounts)
        {
            auto& merged = accounts[address];
            if (account.nonceDirty)
                merged.nonce = account.nonce;
            if (account.balanceDirty)
                merged.balance = account.balance;
            if (!account.code.empty() || account.codeDirty)
            {
                merged.code = account.code;
            }
            if (account.codeDirty)
            {
                merged.codeHash = account.codeHash;
            }
            for (auto const& [slot, value] : account.storage)
            {
                if (state::isZeroBytes32(value))
                {
                    merged.storage.erase(slot);
                }
                else
                {
                    merged.storage[slot] = value;
                }
            }
        }
    }

    (void)accounts[coinbase];

    if (eip158)
    {
        for (auto it = accounts.begin(); it != accounts.end();)
        {
            if (isEmptyAccount(it->second))
            {
                it = accounts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    GstPostStateView view;
    view.eip158ClearEmpty = eip158;
    view.accounts.reserve(accounts.size());
    for (auto& [address, account] : accounts)
    {
        view.accounts.emplace_back(address, std::move(account));
    }
    return view;
}

evmc_bytes32 computeStateRoot(GstPostStateView const& postState)
{
    return computeStateRootImpl(postState);
}

evmc_bytes32 computeLogsHash(std::vector<state::LogEntry> const& logs)
{
    return computeLogsHashImpl(logs);
}

evmc_bytes32 computeTxRoot(std::span<const bcos::bytes> signedTxRlps)
{
    return computeIndexedTrieRoot(signedTxRlps.size(), [&](size_t i) { return signedTxRlps[i]; });
}

evmc_bytes32 computeReceiptsRoot(std::span<const ReceiptForRoot> receipts)
{
    return computeIndexedTrieRoot(
        receipts.size(), [&](size_t i) { return encodeReceipt(receipts[i]); });
}

evmc_bytes32 computeWithdrawalRoot(std::span<const Withdrawal> withdrawals)
{
    return computeIndexedTrieRoot(
        withdrawals.size(), [&](size_t i) { return encodeWithdrawal(withdrawals[i]); });
}

evmc_bytes32 computeRequestsHash(std::span<const bcos::bytes> requests)
{
    bcos::bytes concatenated;
    for (auto const& request : requests)
    {
        if (request.size() <= 1)
        {
            continue;
        }
        auto const inner = bcos::crypto::sha256Hash(bcos::ref(request));
        concatenated.insert(concatenated.end(), inner.begin(), inner.end());
    }
    return sha256ToBytes32(bcos::crypto::sha256Hash(bcos::ref(concatenated)));
}

}  // namespace bcos::evm::reference_tests
