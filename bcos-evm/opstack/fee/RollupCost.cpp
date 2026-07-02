#include "bcos-evm/opstack/fee/RollupCost.h"

#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace bcos::evm
{
namespace
{
// FlzCompressLen implementation.
uint32_t flzCompressLenImpl(bcos::bytesConstRef ib)
{
    uint32_t n = 0;
    std::array<uint32_t, 8192> ht{};

    auto const* const bytes = ib.data();
    auto const len = static_cast<uint32_t>(ib.size());

    auto u24 = [&](uint32_t i) -> uint32_t {
        return static_cast<uint32_t>(bytes[i]) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
               (static_cast<uint32_t>(bytes[i + 2]) << 16);
    };
    auto cmp = [&](uint32_t p, uint32_t q, uint32_t e) -> uint32_t {
        uint32_t l = 0;
        for (e -= q; l < e; ++l)
        {
            if (bytes[p + l] != bytes[q + l])
            {
                e = 0;
            }
        }
        return l;
    };
    auto literals = [&](uint32_t r) {
        n += 0x21 * (r / 0x20);
        r %= 0x20;
        if (r != 0)
        {
            n += r + 1;
        }
    };
    auto match = [&](uint32_t l) {
        --l;
        n += 3 * (l / 262);
        if (l % 262 >= 6)
        {
            n += 3;
        }
        else
        {
            n += 2;
        }
    };
    auto hash = [](uint32_t v) -> uint32_t { return ((2654435769U * v) >> 19) & 0x1fff; };
    auto setNextHash = [&](uint32_t ip) -> uint32_t {
        ht[hash(u24(ip))] = ip;
        return ip + 1;
    };

    uint32_t a = 0;
    uint32_t ipLimit = len - 13;
    if (len < 13)
    {
        ipLimit = 0;
    }

    for (uint32_t ip = a + 2; ip < ipLimit;)
    {
        uint32_t r = 0;
        uint32_t d = 0;
        for (;;)
        {
            auto const s = u24(ip);
            auto const h = hash(s);
            r = ht[h];
            ht[h] = ip;
            d = ip - r;
            if (ip >= ipLimit)
            {
                break;
            }
            ++ip;
            if (d <= 0x1fff && s == u24(r))
            {
                break;
            }
        }
        if (ip >= ipLimit)
        {
            break;
        }
        --ip;
        if (ip > a)
        {
            literals(ip - a);
        }
        auto const l = cmp(r + 3, ip + 3, ipLimit + 9);
        match(l);
        ip = setNextHash(setNextHash(ip + l));
        a = ip;
    }
    literals(len - a);
    return n;
}
}  // namespace

uint32_t flzCompressLen(bcos::bytesConstRef data)
{
    return flzCompressLenImpl(data);
}

RollupCostData newRollupCostData(bcos::bytesConstRef serializedTx)
{
    RollupCostData out;
    for (size_t i = 0; i < serializedTx.size(); ++i)
    {
        if (serializedTx[i] == 0)
        {
            ++out.zeroes;
        }
        else
        {
            ++out.ones;
        }
    }
    out.fastLzSize = flzCompressLen(serializedTx);
    return out;
}

bcos::s256 estimatedDASizeScaled(RollupCostData const& data) noexcept
{
    s256 scaled = s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(data.fastLzSize);
    if (scaled < s256(MIN_TX_SIZE_SCALED))
    {
        scaled = s256(MIN_TX_SIZE_SCALED);
    }
    return scaled;
}

uint64_t estimatedDASize(RollupCostData const& data) noexcept
{
    return static_cast<uint64_t>(estimatedDASizeScaled(data) / s256(1'000'000));
}

}  // namespace bcos::evm
