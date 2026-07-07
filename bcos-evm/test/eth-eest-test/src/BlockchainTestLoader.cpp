#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"

#include <bcos-utilities/DataConvertUtility.h>
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

evmc_bytes32 emptyMptHash()
{
    evmc_bytes32 o{};
    uint8_t b[] = {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45, 0xe6, 0x92,
        0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0, 0x01, 0x62, 0x2f, 0xb5,
        0xe3, 0x63, 0xb4, 0x21};
    std::memcpy(o.bytes, b, 32);
    return o;
}

bool uncleHashImpliesOmmers(std::string_view uncleHex)
{
    auto const uh = toBytes32(uncleHex);
    auto const empty = emptyMptHash();
    return std::memcmp(uh.bytes, empty.bytes, 32) != 0;
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
    bi.baseFee = tb.expectedBlockHeader.baseFeePerGas;

    tb.inputBlobGasUsed = tb.expectedBlockHeader.blobGasUsed;
    tb.inputExcessBlobGas = tb.expectedBlockHeader.excessBlobGas;

    if (auto txs = src->get_child_optional("transactions"))
    {
        for (auto const& [_, txTree] : *txs)
            tb.transactions.push_back(parseTransactionTemplate(txTree));
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
                bt.postState.emplace_back(toAddr(addrStr), std::move(acc));
            }
        }
        else if (auto s = t.get_optional<std::string>("postStateHash"))
            bt.postStateHash = toBytes32(*s);

        out.push_back(std::move(bt));
    }
    return out;
}

}  // namespace bcos::evm::reference_tests
