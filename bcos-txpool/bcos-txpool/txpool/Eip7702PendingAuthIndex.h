#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <unordered_map>
#include <vector>

namespace bcos::txpool
{

/// Tracks in-pool EIP-7702 authority addresses (geth legacypool lookup.auths).
class Eip7702PendingAuthIndex
{
public:
    bool hasPendingAuth(bcos::Address const& authority) const noexcept
    {
        auto it = m_refCount.find(authority);
        return it != m_refCount.end() && it->second > 0;
    }

    void track(bcos::crypto::HashType const& txHash, std::vector<bcos::Address> const& authorities)
    {
        if (authorities.empty())
        {
            return;
        }
        m_txAuthorities.emplace(txHash, authorities);
        for (auto const& authority : authorities)
        {
            ++m_refCount[authority];
        }
    }

    void untrack(bcos::crypto::HashType const& txHash)
    {
        auto it = m_txAuthorities.find(txHash);
        if (it == m_txAuthorities.end())
        {
            return;
        }
        for (auto const& authority : it->second)
        {
            auto countIt = m_refCount.find(authority);
            if (countIt == m_refCount.end())
            {
                continue;
            }
            if (--countIt->second == 0)
            {
                m_refCount.erase(countIt);
            }
        }
        m_txAuthorities.erase(it);
    }

    void clear() noexcept
    {
        m_txAuthorities.clear();
        m_refCount.clear();
    }

private:
    std::unordered_map<bcos::crypto::HashType, std::vector<bcos::Address>> m_txAuthorities;
    std::unordered_map<bcos::Address, std::size_t> m_refCount;
};

}  // namespace bcos::txpool
