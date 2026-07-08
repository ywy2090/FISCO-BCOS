#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"

#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

namespace pt = boost::property_tree;

namespace bcos::evm::reference_tests
{
namespace
{
bcos::bytes hexToBytes(std::string_view s)
{
    if (s.starts_with("0x") || s.starts_with("0X"))
        s.remove_prefix(2);
    bcos::bytes out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2)
    {
        char buf[3] = {s[i], s[i + 1], '\0'};
        out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

evmc_address toAddr(std::string_view hex)
{
    evmc_address a{};
    auto b = hexToBytes(hex);
    if (b.size() >= sizeof(a.bytes))
        std::memcpy(a.bytes, b.data(), sizeof(a.bytes));
    return a;
}

evmc_bytes32 toBytes32(std::string_view hex)
{
    evmc_bytes32 o{};
    auto b = hexToBytes(hex);
    if (b.size() > sizeof(o.bytes))
        b.erase(b.begin(), b.end() - sizeof(o.bytes));
    if (!b.empty())
        std::memcpy(o.bytes + sizeof(o.bytes) - b.size(), b.data(), b.size());
    return o;
}

uint64_t toU64(std::string_view s)
{
    if (s.starts_with("0x") || s.starts_with("0X"))
        return std::strtoull(s.data() + 2, nullptr, 16);
    return std::strtoull(s.data(), nullptr, 10);
}

std::optional<std::string> opt(pt::ptree const& j, char const* k)
{
    if (auto v = j.get_optional<std::string>(k))
        return *v;
    return std::nullopt;
}

bool uncleHashImpliesOmmers(std::string_view uncleHex)
{
    // Empty uncle list hash = keccak256(RLP([])) — distinct from EMPTY_MPT trie root.
    static evmc_bytes32 const EMPTY_UNCLE_HASH = [] {
        evmc_bytes32 o{};
        uint8_t b[] = {0x1d, 0xcc, 0x4d, 0xe8, 0xde, 0xc7, 0x5d, 0x7a, 0xab, 0x85, 0xb5, 0x67, 0xb6,
            0xcc, 0xd4, 0x1a, 0xd3, 0x12, 0x45, 0x1b, 0x94, 0x8a, 0x74, 0x13, 0xf0, 0xa1, 0x42,
            0xfd, 0x40, 0xd4, 0x93, 0x47};
        std::memcpy(o.bytes, b, 32);
        return o;
    }();
    auto const uh = toBytes32(uncleHex);
    return std::memcmp(uh.bytes, EMPTY_UNCLE_HASH.bytes, 32) != 0;
}

bcos::bytes stripLeadingZeros(bcos::bytes const& input)
{
    auto it = std::find_if(input.begin(), input.end(), [](uint8_t b) { return b != 0; });
    if (it == input.end())
    {
        return {};
    }
    return bcos::bytes(it, input.end());
}

bcos::bytes addressToBytes(std::string_view hex)
{
    auto bytes = hexToBytes(hex);
    if (bytes.size() >= sizeof(evmc_address))
    {
        if (bytes.size() > sizeof(evmc_address))
        {
            bytes.erase(bytes.begin(), bytes.end() - sizeof(evmc_address));
        }
        return bytes;
    }
    bcos::bytes out(sizeof(evmc_address), 0);
    if (!bytes.empty())
    {
        std::memcpy(out.data() + sizeof(evmc_address) - bytes.size(), bytes.data(), bytes.size());
    }
    return out;
}

bcos::bytes hash32ToBytes(std::string_view hex)
{
    auto bytes = hexToBytes(hex);
    if (bytes.size() >= sizeof(evmc_bytes32))
    {
        if (bytes.size() > sizeof(evmc_bytes32))
        {
            bytes.erase(bytes.begin(), bytes.end() - sizeof(evmc_bytes32));
        }
        return bytes;
    }
    bcos::bytes out(sizeof(evmc_bytes32), 0);
    if (!bytes.empty())
    {
        std::memcpy(out.data() + sizeof(evmc_bytes32) - bytes.size(), bytes.data(), bytes.size());
    }
    return out;
}

bcos::bytes rlpEncodeScalarHex(std::string_view hex)
{
    return rlpEncodeRaw(stripLeadingZeros(hexToBytes(hex)));
}

bcos::bytes rlpEncodeToField(pt::ptree const& tx)
{
    if (auto const to = opt(tx, "to"))
    {
        if (to->empty())
        {
            return rlpEncodeRaw({});
        }
        return rlpEncodeRaw(addressToBytes(*to));
    }
    return rlpEncodeRaw({});
}

bcos::bytes rlpEncodeDataField(pt::ptree const& tx)
{
    if (auto const data = opt(tx, "data"))
    {
        return rlpEncodeRaw(hexToBytes(*data));
    }
    return rlpEncodeRaw({});
}

bcos::bytes rlpEncodeValueField(pt::ptree const& tx)
{
    if (auto const value = opt(tx, "value"))
    {
        return rlpEncodeU256(bcos::fromBigQuantity(*value));
    }
    return rlpEncodeU256(0);
}

bcos::bytes rlpEncodeGasLimitField(pt::ptree const& tx)
{
    if (auto const gasLimit = opt(tx, "gasLimit"))
    {
        return rlpEncodeUint64(toU64(*gasLimit));
    }
    return rlpEncodeUint64(0);
}

bcos::bytes rlpEncodeNonceField(pt::ptree const& tx)
{
    return rlpEncodeUint64(toU64(opt(tx, "nonce").value_or("0x0")));
}

bcos::bytes rlpEncodeChainIdField(pt::ptree const& tx)
{
    if (auto const chainId = opt(tx, "chainId"))
    {
        return rlpEncodeU256(bcos::fromBigQuantity(*chainId));
    }
    return rlpEncodeU256(0);
}

bcos::bytes rlpEncodeParityField(pt::ptree const& tx)
{
    if (auto const yParity = opt(tx, "yParity"))
    {
        return rlpEncodeUint64(toU64(*yParity));
    }
    if (auto const v = opt(tx, "v"))
    {
        return rlpEncodeUint64(toU64(*v));
    }
    return rlpEncodeUint64(0);
}

bcos::bytes rlpEncodeAccessListFromJson(pt::ptree const& tx)
{
    std::vector<bcos::bytes> entries;
    if (auto const listNode = tx.get_child_optional("accessList"))
    {
        for (auto const& [_, entryNode] : *listNode)
        {
            std::vector<bcos::bytes> storageKeys;
            if (auto const storage = entryNode.get_child_optional("storageKeys"))
            {
                for (auto const& [slotKey, slotNode] : *storage)
                {
                    static_cast<void>(slotKey);
                    storageKeys.push_back(
                        rlpEncodeRaw(hash32ToBytes(slotNode.get_value<std::string>())));
                }
            }
            entries.push_back(
                rlpEncodeList({rlpEncodeRaw(addressToBytes(entryNode.get<std::string>("address"))),
                    rlpEncodeList(storageKeys)}));
        }
    }
    return rlpEncodeList(entries);
}

bcos::bytes rlpEncodeAuthorizationListFromJson(pt::ptree const& tx)
{
    std::vector<bcos::bytes> entries;
    if (auto const authNode = tx.get_child_optional("authorizationList"))
    {
        for (auto const& [_, entryNode] : *authNode)
        {
            bcos::bytes yParity = rlpEncodeParityField(entryNode);
            bcos::bytes sigR = opt(entryNode, "r").has_value() ?
                                   rlpEncodeScalarHex(*opt(entryNode, "r")) :
                                   rlpEncodeRaw({});
            bcos::bytes sigS = opt(entryNode, "s").has_value() ?
                                   rlpEncodeScalarHex(*opt(entryNode, "s")) :
                                   rlpEncodeRaw({});
            entries.push_back(rlpEncodeList(
                {rlpEncodeU256(bcos::fromBigQuantity(entryNode.get<std::string>("chainId", "0x0"))),
                    rlpEncodeRaw(addressToBytes(entryNode.get<std::string>("address"))),
                    rlpEncodeUint64(toU64(entryNode.get<std::string>("nonce", "0x0"))),
                    std::move(yParity), std::move(sigR), std::move(sigS)}));
        }
    }
    return rlpEncodeList(entries);
}

bcos::bytes rlpEncodeBlobVersionedHashesFromJson(pt::ptree const& tx)
{
    std::vector<bcos::bytes> hashes;
    if (auto const blobHashes = tx.get_child_optional("blobVersionedHashes"))
    {
        for (auto const& [key, hashNode] : *blobHashes)
        {
            static_cast<void>(key);
            hashes.push_back(rlpEncodeRaw(hash32ToBytes(hashNode.get_value<std::string>())));
        }
    }
    return rlpEncodeList(hashes);
}

uint8_t txTypeFromJson(pt::ptree const& tx)
{
    if (auto const type = opt(tx, "type"))
    {
        return static_cast<uint8_t>(toU64(*type));
    }
    return 0;
}

bcos::bytes encodeSignedTxFromJson(pt::ptree const& tx)
{
    auto const nonce = rlpEncodeNonceField(tx);
    auto const gasLimit = rlpEncodeGasLimitField(tx);
    auto const to = rlpEncodeToField(tx);
    auto const value = rlpEncodeValueField(tx);
    auto const data = rlpEncodeDataField(tx);
    auto const sigR =
        opt(tx, "r").has_value() ? rlpEncodeScalarHex(*opt(tx, "r")) : rlpEncodeRaw({});
    auto const sigS =
        opt(tx, "s").has_value() ? rlpEncodeScalarHex(*opt(tx, "s")) : rlpEncodeRaw({});

    auto const type = txTypeFromJson(tx);
    bcos::bytes body;
    switch (type)
    {
    case 0:
        body = rlpEncodeList(
            {nonce, rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "gasPrice").value_or("0x0"))),
                gasLimit, to, value, data,
                opt(tx, "v").has_value() ? rlpEncodeScalarHex(*opt(tx, "v")) : rlpEncodeUint64(0),
                sigR, sigS});
        return body;
    case 1:
        body = rlpEncodeList({rlpEncodeChainIdField(tx), nonce,
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "gasPrice").value_or("0x0"))), gasLimit, to,
            value, data, rlpEncodeAccessListFromJson(tx), rlpEncodeParityField(tx), sigR, sigS});
        break;
    case 2:
        body = rlpEncodeList({rlpEncodeChainIdField(tx), nonce,
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxPriorityFeePerGas").value_or("0x0"))),
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxFeePerGas").value_or("0x0"))), gasLimit,
            to, value, data, rlpEncodeAccessListFromJson(tx), rlpEncodeParityField(tx), sigR,
            sigS});
        break;
    case 3:
        body = rlpEncodeList({rlpEncodeChainIdField(tx), nonce,
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxPriorityFeePerGas").value_or("0x0"))),
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxFeePerGas").value_or("0x0"))), gasLimit,
            to, value, data, rlpEncodeAccessListFromJson(tx),
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxFeePerBlobGas").value_or("0x0"))),
            rlpEncodeBlobVersionedHashesFromJson(tx), rlpEncodeParityField(tx), sigR, sigS});
        break;
    case 4:
        body = rlpEncodeList({rlpEncodeChainIdField(tx), nonce,
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxPriorityFeePerGas").value_or("0x0"))),
            rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "maxFeePerGas").value_or("0x0"))), gasLimit,
            to, value, data, rlpEncodeAccessListFromJson(tx),
            rlpEncodeAuthorizationListFromJson(tx), rlpEncodeParityField(tx), sigR, sigS});
        break;
    default:
        body = rlpEncodeList(
            {nonce, rlpEncodeU256(bcos::fromBigQuantity(opt(tx, "gasPrice").value_or("0x0"))),
                gasLimit, to, value, data,
                opt(tx, "v").has_value() ? rlpEncodeScalarHex(*opt(tx, "v")) : rlpEncodeUint64(0),
                sigR, sigS});
        return body;
    }

    bcos::bytes out;
    out.push_back(type);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

BlobSchedule parseBlobSchedule(pt::ptree const& scheduleTree)
{
    BlobSchedule out;
    for (auto const& [network, params] : scheduleTree)
    {
        BlobParams p;
        if (auto s = opt(params, "target"))
            p.target = static_cast<uint16_t>(toU64(*s));
        if (auto s = opt(params, "max"))
            p.max = static_cast<uint16_t>(toU64(*s));
        if (auto s = opt(params, "baseFeeUpdateFraction"))
            p.baseFeeUpdateFraction = static_cast<uint32_t>(toU64(*s));
        out.emplace(network, p);
    }
    return out;
}
}  // namespace

TestBlockHeader parseBlockHeader(pt::ptree const& j)
{
    TestBlockHeader h;
    if (auto s = opt(j, "parentHash"))
        h.parentHash = toBytes32(*s);
    if (auto s = opt(j, "coinbase"))
        h.coinbase = toAddr(*s);
    if (auto s = opt(j, "stateRoot"))
        h.stateRoot = toBytes32(*s);
    if (auto s = opt(j, "receiptTrie"))
        h.receiptsRoot = toBytes32(*s);
    if (auto s = opt(j, "bloom"))
        h.logsBloom = hexToBytes(*s);
    if (auto s = opt(j, "difficulty"))
        h.difficulty = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "mixHash"))
        h.prevRandao = toBytes32(*s);
    if (auto s = opt(j, "number"))
        h.blockNumber = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "gasLimit"))
        h.gasLimit = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "gasUsed"))
        h.gasUsed = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "timestamp"))
        h.timestamp = static_cast<int64_t>(toU64(*s));
    if (auto s = opt(j, "extraData"))
        h.extraData = hexToBytes(*s);
    if (auto s = opt(j, "baseFeePerGas"))
        h.baseFeePerGas = toU64(*s);
    if (auto s = opt(j, "hash"))
        h.hash = toBytes32(*s);
    if (auto s = opt(j, "transactionsTrie"))
        h.transactionsRoot = toBytes32(*s);
    if (auto s = opt(j, "withdrawalsRoot"))
        h.withdrawalsRoot = toBytes32(*s);
    if (auto s = opt(j, "parentBeaconBlockRoot"))
        h.parentBeaconBlockRoot = toBytes32(*s);
    if (auto s = opt(j, "blobGasUsed"))
        h.blobGasUsed = toU64(*s);
    if (auto s = opt(j, "excessBlobGas"))
        h.excessBlobGas = toU64(*s);
    if (auto s = opt(j, "requestsHash"))
        h.requestsHash = toBytes32(*s);
    if (auto s = opt(j, "slotNumber"))
        h.slotNumber = toU64(*s);
    return h;
}

TestBlock parseTestBlock(pt::ptree const& j, std::string_view /*network*/)
{
    TestBlock tb;
    tb.expectException = j.get<std::string>("expectException", "");

    pt::ptree const* src = &j;
    if (auto rd = j.get_child_optional("rlp_decoded"))
        src = &rd.get();

    if (auto hdr = src->get_child_optional("blockHeader"))
    {
        tb.hasStructuredHeader = true;
        tb.expectedBlockHeader = parseBlockHeader(*hdr);
        if (auto s = opt(*hdr, "uncleHash"))
            tb.hasOmmers = uncleHashImpliesOmmers(*s);
    }

    auto& bi = tb.blockInfo;
    bi.number = tb.expectedBlockHeader.blockNumber;
    bi.timestamp = tb.expectedBlockHeader.timestamp;
    bi.gasLimit = tb.expectedBlockHeader.gasLimit;
    bi.coinbase = tb.expectedBlockHeader.coinbase;
    bi.prevRandao = tb.expectedBlockHeader.prevRandao;
    bi.parentHash = tb.expectedBlockHeader.parentHash;
    bi.parentBeaconBlockRoot = tb.expectedBlockHeader.parentBeaconBlockRoot;
    bi.baseFee = tb.expectedBlockHeader.baseFeePerGas;

    tb.inputBlobGasUsed = tb.expectedBlockHeader.blobGasUsed;
    tb.inputExcessBlobGas = tb.expectedBlockHeader.excessBlobGas;

    if (auto txs = src->get_child_optional("transactions"))
    {
        for (auto const& [_, txTree] : *txs)
        {
            tb.transactions.push_back(parseTransactionTemplate(txTree));
            tb.rawTxRlp.push_back(encodeSignedTxFromJson(txTree));
        }
    }

    if (auto ws = src->get_child_optional("withdrawals"))
    {
        for (auto const& [_, w] : *ws)
        {
            Withdrawal wd;
            if (auto s = opt(w, "index"))
                wd.index = toU64(*s);
            if (auto s = opt(w, "validatorIndex"))
                wd.validatorIndex = toU64(*s);
            if (auto s = opt(w, "address"))
                wd.address = toAddr(*s);
            if (auto s = opt(w, "amount"))
                wd.amount = toU64(*s);
            tb.withdrawals.push_back(wd);
        }
    }

    if (auto rlp = opt(j, "rlp"))
        tb.rlpSize = hexToBytes(*rlp).size();

    return tb;
}

std::vector<BlockchainTest> loadBlockchainTests(pt::ptree const& root)
{
    std::vector<BlockchainTest> out;
    for (auto const& [name, t] : root)
    {
        if (!t.count("pre") || !t.count("genesisBlockHeader"))
            continue;

        BlockchainTest bt;
        bt.name = name;
        bt.network = t.get<std::string>("network", "");
        bt.genesisBlockHeader = parseBlockHeader(t.get_child("genesisBlockHeader"));

        if (auto cfg = t.get_child_optional("config"))
        {
            if (auto s = opt(*cfg, "chainid"))
                bt.chainId = bcos::fromBigQuantity(*s);
            if (auto bs = cfg->get_child_optional("blobSchedule"))
                bt.blobSchedule = parseBlobSchedule(*bs);
        }

        for (auto const& [addrStr, accTree] : t.get_child("pre"))
        {
            state::Account acc{};
            if (auto s = opt(accTree, "nonce"))
                acc.nonce = toU64(*s);
            if (auto s = opt(accTree, "balance"))
                acc.balance = bcos::fromBigQuantity(*s);
            if (auto s = opt(accTree, "code"))
                acc.code = hexToBytes(*s);
            if (auto st = accTree.get_child_optional("storage"))
            {
                for (auto const& [k, v] : *st)
                    acc.storage[toBytes32(k)] = toBytes32(v.get_value<std::string>());
            }
            bt.preState.insertAccount(toAddr(addrStr), std::move(acc));
        }

        if (auto blocks = t.get_child_optional("blocks"))
        {
            for (auto const& [_, b] : *blocks)
                bt.testBlocks.push_back(parseTestBlock(b, bt.network));
        }

        if (auto s = t.get_optional<std::string>("lastblockhash"))
            bt.lastBlockHash = toBytes32(*s);

        if (auto ps = t.get_child_optional("postState"))
        {
            for (auto const& [addrStr, accTree] : *ps)
            {
                auto const addr = toAddr(addrStr);

                // Raw state::Account (legacy probes).
                state::Account acc{};
                // Presence-aware expectation (normative path).
                ExpectedPostAccount exp;

                if (auto s = opt(accTree, "nonce"))
                {
                    acc.nonce = toU64(*s);
                    exp.hasNonce = true;
                    exp.nonce = acc.nonce;
                }
                if (auto s = opt(accTree, "balance"))
                {
                    acc.balance = bcos::fromBigQuantity(*s);
                    exp.hasBalance = true;
                    exp.balance = acc.balance;
                }
                if (auto s = opt(accTree, "code"))
                {
                    acc.code = hexToBytes(*s);
                    exp.hasCode = true;
                    exp.code = acc.code;
                }
                if (auto st = accTree.get_child_optional("storage"))
                {
                    exp.hasStorage = true;
                    for (auto const& [k, v] : *st)
                    {
                        auto slot = toBytes32(k);
                        auto val = toBytes32(v.get_value<std::string>());
                        acc.storage[slot] = val;
                        exp.storage.emplace_back(slot, val);
                    }
                }
                // NOTE: boost::property_tree cannot distinguish JSON null from {} (Task 0);
                // absent accounts are not derivable from the corpus here and stay Present.
                // TODO: if a future EEST pin introduces explicit `null` postState accounts
                // (absent), switch to a null-preserving parser and emit
                // ExpectedPostAccount::Kind::Absent; property_tree currently maps them to
                // presence-only Present.
                bt.postState.emplace_back(addr, std::move(acc));
                bt.postExpectation.accounts.emplace_back(addr, std::move(exp));
            }
        }
        else if (auto s = t.get_optional<std::string>("postStateHash"))
        {
            bt.postStateHash = toBytes32(*s);
            bt.postExpectation.hash = bt.postStateHash;
        }

        out.push_back(std::move(bt));
    }
    return out;
}

}  // namespace bcos::evm::reference_tests
