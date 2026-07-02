/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file FrameBytecode.cpp
 */

#include "bcos-evm/eth/kernel/execution/FrameBytecode.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{

bcos::bytes loadFrameBytecode(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message const& msg, evmc_address executionAddress)
{
    // CREATE paths never read state code; initcode lives in msg.input (geth: init code buffer).
    if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
    {
        if (msg.input_data == nullptr || msg.input_size == 0)
        {
            return {};
        }
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }

    auto code = state.get_code(executionAddress);
    // EIP-7702: authority account may store a delegation designator instead of runtime bytecode.
    // Follow one hop to the delegate's code; routing already picked executionAddress.
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
