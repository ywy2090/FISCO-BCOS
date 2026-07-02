/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file CreateAddress.cpp
 */

#include "bcos-evm/eth/kernel/execution/CreateAddress.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <cstring>
#include <evmone_precompiles/keccak.hpp>

namespace bcos::evm::execution
{

evmc_address predictCreate2Address(
    evmc_address const& sender, evmc_bytes32 const& salt, bcos::bytesConstRef initCode) noexcept
{
    bcos::bytes buffer;
    buffer.reserve(1 + sizeof(sender.bytes) + sizeof(salt.bytes) + 32);
    buffer.push_back(0xff);
    buffer.insert(buffer.end(), sender.bytes, sender.bytes + sizeof(sender.bytes));
    buffer.insert(buffer.end(), salt.bytes, salt.bytes + sizeof(salt.bytes));
    auto const initHash = ethash::keccak256(initCode.data(), initCode.size());
    buffer.insert(buffer.end(), initHash.bytes, initHash.bytes + sizeof(initHash.bytes));
    auto const hash = ethash::keccak256(buffer.data(), buffer.size());
    evmc_address out{};
    std::memcpy(out.bytes, hash.bytes + 12, sizeof(out.bytes));
    return out;
}

evmc_address predictCreateAddress(
    state::State& st, evmc_message const& message, bcos::bytesConstRef initCode) noexcept
{
    if (!state::isZeroAddress(message.recipient))
    {
        return message.recipient;
    }
    if (!state::isZeroAddress(message.code_address))
    {
        return message.code_address;
    }
    if (message.kind == EVMC_CREATE2)
    {
        return predictCreate2Address(message.sender, message.create2_salt, initCode);
    }
    return bcos::evm::state::predictLegacyCreateAddress(
        message.sender, st.get_nonce(message.sender));
}

}  // namespace bcos::evm::execution
