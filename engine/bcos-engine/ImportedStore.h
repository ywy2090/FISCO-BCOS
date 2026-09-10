/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file ImportedStore.h
 * @brief Hash-keyed in-memory store for engine-imported (not yet canonical) OP
 * payloads — the S5 `InsertBlockWithoutSetHead` side. Blocks land here by HASH;
 * nothing in this store touches NUMBER_2_HASH / SYS_CURRENT_STATE (design §4.2).
 */
#pragma once

#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/Common.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace bcos::engine
{
struct ImportedBlock
{
    bcos::h256 hash;
    bcos::h256 parent;
    bcos::protocol::BlockNumber number{};
    bcos::bytes headerBytes;  // encoded header, read back BY HASH (never by number)
    std::vector<bcos::bytes> txs;        // ordered signed envelopes
    std::vector<bcos::bytes> receipts;
    // Per-block storage delta relative to parent. Task 3 replaces this placeholder
    // with the real executor delta type; `put` success == the delta exists.
    std::shared_ptr<void> storageDelta;
};

/// Imported payloads only: canonical-chain lookups must go through the ledger
/// tables first, this store is the OP-side fallback (design §4.1 read path).
/// NOT thread-safe by itself — the engine lock guards put/canonicalize/applyForkchoice.
class ImportedStore
{
public:
    /// Same-hash re-put is idempotent (newPayload replay stays VALID, no double
    /// write). Fails when the same-height slot is occupied by a block that already
    /// has imported descendants: overwriting their ancestor would orphan the chain
    /// state (newPayload maps this to SYNCING, design §4.2 单分叉冲突).
    bool put(ImportedBlock block)
    {
        if (m_blocks.contains(block.hash))
        {
            return true;
        }
        // Same-height occupant with an imported child rejects the overwrite.
        for (auto const& [hash, existing] : m_blocks)
        {
            if (existing.number != block.number)
            {
                continue;
            }
            for (auto const& [childHash, child] : m_blocks)
            {
                if (child.parent == hash)
                {
                    return false;
                }
            }
        }
        m_blocks.emplace(block.hash, std::move(block));
        return true;
    }

    [[nodiscard]] bool hasBlock(const bcos::h256& hash) const { return m_blocks.contains(hash); }
    /// Every stored block was executed on its parent's post-state, so a stored
    /// block always has state (design: put 成功才 hasState).
    [[nodiscard]] bool hasState(const bcos::h256& hash) const { return hasBlock(hash); }
    [[nodiscard]] std::optional<std::vector<bcos::bytes>> body(const bcos::h256& hash) const
    {
        auto const it = m_blocks.find(hash);
        if (it == m_blocks.end())
        {
            return std::nullopt;
        }
        return it->second.txs;
    }
    [[nodiscard]] std::optional<ImportedBlock> get(const bcos::h256& hash) const
    {
        auto const it = m_blocks.find(hash);
        if (it == m_blocks.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
    [[nodiscard]] std::size_t size() const { return m_blocks.size(); }

private:
    std::unordered_map<bcos::h256, ImportedBlock> m_blocks;
};
}  // namespace bcos::engine
