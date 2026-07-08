#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"

#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{
namespace
{
bool bytes32Equal(evmc_bytes32 const& a, evmc_bytes32 const& b)
{
    return std::memcmp(a.bytes, b.bytes, 32) == 0;
}

bool isZero(evmc_bytes32 const& v)
{
    for (auto b : v.bytes)
        if (b != 0)
            return false;
    return true;
}

std::string hexAddr(evmc_address const& a)
{
    return "0x" + bcos::toHex(bcos::bytes(a.bytes, a.bytes + sizeof(a.bytes)));
}

std::string hex32(evmc_bytes32 const& v)
{
    return "0x" + bcos::toHex(bcos::bytes(v.bytes, v.bytes + sizeof(v.bytes)));
}

std::string hexBytes(bcos::bytes const& b)
{
    // truncate long code for messages
    if (b.size() > 32)
        return "0x" + bcos::toHex(bcos::bytes(b.begin(), b.begin() + 32)) + "…";
    return "0x" + bcos::toHex(b);
}

evmc_bytes32 storageOf(state::Account const& acc, evmc_bytes32 const& slot)
{
    auto it = acc.storage.find(slot);
    if (it != acc.storage.end())
        return it->second;
    return evmc_bytes32{};
}

evmc_bytes32 computeRootFromView(TestStateView const& view, bool eip158)
{
    GstPostStateView gv;
    gv.eip158ClearEmpty = eip158;
    for (auto const& [addr, acc] : view.accounts())
        gv.accounts.emplace_back(addr, acc);
    return computeStateRoot(gv);
}

void record(PostStateAssertReport& r, PostStateFieldDiff diff)
{
    if (r.summary.empty())
        r.summary = diff.message;
    r.diffs.push_back(std::move(diff));
    r.passed = false;
}
}  // namespace

PostStateAssertReport assertPostState(
    TestStateView const& actual, PostStateExpectation const& expected, AssertOptions const& opts)
{
    PostStateAssertReport report;

    // Precedence: partial account map wins over hash (spec §4.3).
    if (expected.accounts.empty())
    {
        if (expected.hash.has_value())
        {
            auto computed = computeRootFromView(actual, opts.eip158ClearEmpty);
            if (!bytes32Equal(computed, *expected.hash))
            {
                PostStateFieldDiff d;
                d.field = "postStateHash";
                d.got = hex32(computed);
                d.want = hex32(*expected.hash);
                d.message = "postStateHash mismatch got=" + d.got + " want=" + d.want;
                record(report, std::move(d));
            }
        }
        return report;
    }

    for (auto const& [address, exp] : expected.accounts)
    {
        auto got = actual.get_account(address);

        if (exp.kind == ExpectedPostAccount::Kind::Absent)
        {
            if (got.has_value())
            {
                PostStateFieldDiff d;
                d.address = address;
                d.field = "existence";
                d.got = "present";
                d.want = "absent";
                d.message = "postState account should be absent addr=" + hexAddr(address);
                record(report, std::move(d));
            }
            continue;
        }

        if (!got.has_value())
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "existence";
            d.got = "absent";
            d.want = "present";
            d.message = "postState account missing addr=" + hexAddr(address);
            record(report, std::move(d));
            continue;
        }

        auto const& acc = *got;

        if (exp.hasNonce && acc.nonce != exp.nonce)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "nonce";
            d.got = std::to_string(acc.nonce);
            d.want = std::to_string(exp.nonce);
            d.message = "postState nonce mismatch addr=" + hexAddr(address) + " got=" + d.got +
                        " want=" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasBalance && acc.balance != exp.balance)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "balance";
            d.got = "0x" + bcos::toHex(bcos::toCompactBigEndian(acc.balance));
            d.want = "0x" + bcos::toHex(bcos::toCompactBigEndian(exp.balance));
            d.message = "postState balance mismatch addr=" + hexAddr(address) + " got=" + d.got +
                        " want=" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasCode && acc.code != exp.code)
        {
            PostStateFieldDiff d;
            d.address = address;
            d.field = "code";
            d.got = hexBytes(acc.code);
            d.want = hexBytes(exp.code);
            d.message = "postState code mismatch addr=" + hexAddr(address) + " got=" + d.got +
                        " want=" + d.want;
            record(report, std::move(d));
        }

        if (exp.hasStorage)
        {
            if (exp.storage.empty())
            {
                // storage: {} ⇒ every actual slot must be zero.
                // acc.storage is an unordered_map; sort by slot key (memcmp) so the
                // reported first-diff is deterministic (spec §4.4).
                std::vector<std::pair<evmc_bytes32, evmc_bytes32>> sortedStorage(
                    acc.storage.begin(), acc.storage.end());
                std::sort(sortedStorage.begin(), sortedStorage.end(),
                    [](auto const& lhs, auto const& rhs) {
                        return std::memcmp(lhs.first.bytes, rhs.first.bytes, 32) < 0;
                    });
                for (auto const& [slot, val] : sortedStorage)
                {
                    if (!isZero(val))
                    {
                        PostStateFieldDiff d;
                        d.address = address;
                        d.field = "storage";
                        d.slot = slot;
                        d.got = hex32(val);
                        d.want = "0x0";
                        d.message = "postState storage should be empty addr=" + hexAddr(address) +
                                    " slot=" + hex32(slot) + " got=" + d.got;
                        record(report, std::move(d));
                    }
                }
            }
            else
            {
                for (auto const& [slot, want] : exp.storage)
                {
                    auto val = storageOf(acc, slot);
                    if (!bytes32Equal(val, want))
                    {
                        PostStateFieldDiff d;
                        d.address = address;
                        d.field = "storage";
                        d.slot = slot;
                        d.got = hex32(val);
                        d.want = hex32(want);
                        d.message = "postState storage mismatch addr=" + hexAddr(address) +
                                    " slot=" + hex32(slot) + " got=" + d.got + " want=" + d.want;
                        record(report, std::move(d));
                    }
                }
            }
        }
    }

    return report;
}

}  // namespace bcos::evm::reference_tests
