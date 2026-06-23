/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile dispatch router (Phase 1 skeleton).
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/policy/HostExtension.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.hpp>

namespace bcos::evm::precompiled
{

enum class PrecompileDispatchOutcome
{
    NotApplicable,
    Dispatched,
    EmptyAccountSuccess
};

struct PrecompileRouterInput
{
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    state::HostExtension* extension;
    evmc_message const& message;
    evmc_address target;
    bool skipValueTransfer;
};

struct PrecompileRouterOutput
{
    PrecompileDispatchOutcome outcome{PrecompileDispatchOutcome::NotApplicable};
    evmc::Result result{};
    int64_t gasRefund{0};
};

PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input);

}  // namespace bcos::evm::precompiled
