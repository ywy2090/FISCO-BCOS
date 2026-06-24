/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 */

#define BOOST_TEST_MODULE StateJournalRevertTest
#include "bcos-evm/eth/state/State.hpp"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::state::test
{
namespace
{
bool bytes32Equal(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs)
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

class MockStateView : public StateView
{
public:
    std::optional<Account> get_account(const evmc_address& address) const override
    {
        if (address.bytes[19] == 0x01)
        {
            Account account;
            account.balance = 100;
            account.storage[evmc_bytes32{}] = evmc_bytes32{};
            return account;
        }
        return std::nullopt;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(StateJournalRevertTest)

BOOST_AUTO_TEST_CASE(revert_discards_post_checkpoint_writes)
{
    MockStateView view;
    State state(view);

    evmc_address address{};
    address.bytes[19] = 0x01;

    evmc_bytes32 keyA{};
    keyA.bytes[31] = 0x0A;
    evmc_bytes32 valueA{};
    valueA.bytes[31] = 0xAA;

    evmc_bytes32 keyB{};
    keyB.bytes[31] = 0x0B;
    evmc_bytes32 valueB{};
    valueB.bytes[31] = 0xBB;

    state.set_balance(address, 101);
    state.set_storage(address, keyA, valueA);

    state.checkpoint();
    state.set_balance(address, 202);
    state.set_storage(address, keyB, valueB);
    state.revert();

    BOOST_CHECK_EQUAL(state.get_balance(address), 101);
    BOOST_CHECK(bytes32Equal(state.get_storage(address, keyA), valueA));
    BOOST_CHECK(bytes32Equal(state.get_storage(address, keyB), evmc_bytes32{}));
}

BOOST_AUTO_TEST_CASE(revert_discards_post_checkpoint_transient_storage)
{
    MockStateView view;
    State state(view);

    evmc_address address{};
    address.bytes[19] = 0x01;

    evmc_bytes32 key{};
    key.bytes[31] = 0x0C;
    evmc_bytes32 valueBefore{};
    valueBefore.bytes[31] = 0xCC;
    evmc_bytes32 valueAfter{};
    valueAfter.bytes[31] = 0xDD;

    state.set_transient_storage(address, key, valueBefore);
    state.checkpoint();
    state.set_transient_storage(address, key, valueAfter);
    state.revert();

    auto account = state.find(address);
    BOOST_REQUIRE(account.has_value());
    auto it = account->transientStorage.find(key);
    BOOST_REQUIRE(it != account->transientStorage.end());
    BOOST_CHECK(bytes32Equal(it->second, valueBefore));
}

BOOST_AUTO_TEST_CASE(revert_discards_post_checkpoint_warm_sets)
{
    MockStateView view;
    State state(view);

    evmc_address address{};
    address.bytes[19] = 0x02;
    evmc_bytes32 key{};
    key.bytes[31] = 0x0D;

    state.checkpoint();
    BOOST_REQUIRE(state.warm_up_address(address));
    BOOST_REQUIRE(state.warm_up_storage(address, key));
    BOOST_CHECK(state.is_address_warm(address));
    BOOST_CHECK(state.is_storage_warm(address, key));

    state.revert();
    BOOST_CHECK(!state.is_address_warm(address));
    BOOST_CHECK(!state.is_storage_warm(address, key));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::state::test
