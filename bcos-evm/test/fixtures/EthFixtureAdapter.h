/*
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
 * @brief Maps EthStateFixtureLoader cases to EthMessageRequest.
 * @file EthFixtureAdapter.h
 */

#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/apply/EthMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"

namespace bcos::evm::test::fixtures
{

inline bcos::evm_standard::RevisionConfig makePragueRevisionConfig()
{
    bcos::evm_standard::RevisionConfig cfg;
    cfg.revision = EVMC_PRAGUE;
    cfg.eip2929 = true;
    cfg.eip1153 = true;
    cfg.eip4844 = true;
    cfg.eip5656 = true;
    cfg.eip6780 = true;
    cfg.eip2537 = true;
    cfg.eip7623 = true;
    cfg.eip7702 = true;
    cfg.calldata_floor_per_token = 10;
    return cfg;
}

inline EthMessageRequest buildEthMessageRequest(FixtureCase const& fixture,
    state::StateView const& stateView, evmc::VM& vm, bcos::crypto::Hash const& hashImpl)
{
    EthMessageRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;

    evmc_message msg{};
    msg.kind = fixture.tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.flags = fixture.txProps.isStatic ? EVMC_STATIC : 0;
    msg.depth = 0;
    msg.gas = fixture.tx.gasLimit;
    msg.sender = fixture.tx.from;
    msg.recipient = fixture.tx.to.value_or(evmc_address{});
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();
    msg.value = state::toEvmC(fixture.tx.value);
    msg.create2_salt = {};
    input.message = msg;

    input.blockInfo = fixture.block;
    input.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    input.revisionConfig = makePragueRevisionConfig();
    input.gasPrice = fixture.tx.gasPrice;
    input.gasTipCap = fixture.tx.gasPrice;
    input.gasFeeCap = fixture.tx.gasPrice;
    input.authorizationListPresent = fixture.authorizationListPresent;
    input.authorizations = fixture.authorizations;
    input.txNonce = fixture.tx.nonce;

    return input;
}

}  // namespace bcos::evm::test::fixtures
