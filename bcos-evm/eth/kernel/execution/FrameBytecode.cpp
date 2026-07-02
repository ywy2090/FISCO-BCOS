/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file FrameBytecode.cpp
 */

#include "bcos-evm/eth/kernel/execution/FrameBytecode.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/CallKind.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{

bcos::bytes loadFrameBytecode(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message const& msg, evmc_address executionAddress)
{
    if (isCreateKind(msg.kind))
    {
        if (msg.input_data == nullptr || msg.input_size == 0)
        {
            return {};
        }
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }

    auto code = state.get_code(executionAddress);
    if (revisionConfig.eip7702)
    {
        if (auto const delegate =
                parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
        {
            return state.get_code(*delegate);
        }
    }
    return code;
}

}  // namespace bcos::evm::execution
