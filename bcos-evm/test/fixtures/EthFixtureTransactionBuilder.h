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
 * @brief Builds bcostars TransactionImpl from EthStateFixtureLoader cases.
 * @file EthFixtureTransactionBuilder.h
 */

#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <span>

namespace bcos::evm::test::fixtures
{

inline std::string addressToHexLower(evmc_address const& addr)
{
    return bcos::toHex(std::span(addr.bytes, sizeof(addr.bytes)));
}

inline std::string u256ToHex(bcos::u256 const& v)
{
    if (v == 0)
    {
        return "0x0";
    }
    return "0x" + bcos::toHex(bcos::toCompactBigEndian(v));
}

inline std::shared_ptr<bcostars::protocol::TransactionImpl> buildFixtureTransaction(
    FixtureCase const& fixture, bcostars::protocol::TransactionFactoryImpl& factory)
{
    std::string to;
    if (fixture.tx.to.has_value())
    {
        to = addressToHexLower(*fixture.tx.to);
    }
    auto tx = factory.createTransaction(0,  // version
        to,                                 // to (empty = CREATE)
        fixture.tx.data,                    // input
        std::to_string(fixture.tx.nonce),   // nonce
        999'999'999,                        // blockLimit
        "1",                                // chainId
        "",                                 // groupId
        0,                                  // importTime
        "",                                 // abi
        u256ToHex(fixture.tx.value),        // value
        "0x0",                              // gasPrice
        fixture.tx.gasLimit,                // gasLimit
        "0x0",                              // maxFeePerGas
        "0x0");                             // maxPriorityFeePerGas
    tx->forceSender(
        bcos::bytes(fixture.tx.from.bytes, fixture.tx.from.bytes + sizeof(fixture.tx.from.bytes)));
    return std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
}

}  // namespace bcos::evm::test::fixtures
