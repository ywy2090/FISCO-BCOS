#define BOOST_TEST_MODULE SstoreStatusTest
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <vector>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 valueFromLastByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SstoreStatusTest)

BOOST_AUTO_TEST_CASE(fisco_sstore_status_matrix_matches_fix_flag)
{
    struct Case
    {
        bool fixStorageStatus;
        bool existingIsZero;
        bool newIsZero;
        evmc_storage_status expected;
    };

    std::vector<Case> const cases = {
        // fix_storage_status = ON (4-state)
        {true, true, true, EVMC_STORAGE_ASSIGNED},
        {true, false, true, EVMC_STORAGE_DELETED},
        {true, true, false, EVMC_STORAGE_ADDED},
        {true, false, false, EVMC_STORAGE_MODIFIED},
        // fix_storage_status = OFF (2-state)
        {false, true, true, EVMC_STORAGE_DELETED},
        {false, false, true, EVMC_STORAGE_DELETED},
        {false, true, false, EVMC_STORAGE_MODIFIED},
        {false, false, false, EVMC_STORAGE_MODIFIED},
    };

    auto const target = addressFromLastByte(0x33);
    auto const key = valueFromLastByte(0x01);
    auto const nonZero = valueFromLastByte(0x09);
    auto const zero = evmc_bytes32{};

    for (auto const& testCase : cases)
    {
        BOOST_TEST_CONTEXT("fixStorageStatus=" << testCase.fixStorageStatus
                                               << ", existingIsZero=" << testCase.existingIsZero
                                               << ", newIsZero=" << testCase.newIsZero)
        {
            InMemoryStateView view;
            Account account;
            if (!testCase.existingIsZero)
            {
                account.storage[key] = nonZero;
            }
            view.insert_account(target, account);

            State state(view);
            EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, nullptr, testCase.fixStorageStatus);

            auto const newValue = testCase.newIsZero ? zero : nonZero;
            auto const status = host.set_storage(target, key, newValue);

            BOOST_CHECK_EQUAL(static_cast<int>(status), static_cast<int>(testCase.expected));
            BOOST_CHECK(Bytes32Equal{}(state.get_storage(target, key), newValue));
        }
    }
}

BOOST_AUTO_TEST_CASE(fix_on_uses_original_committed_value_for_status)
{
    auto const target = addressFromLastByte(0x44);
    auto const key = valueFromLastByte(0x02);
    auto const zero = evmc_bytes32{};
    auto const firstNonZero = valueFromLastByte(0x05);
    auto const secondNonZero = valueFromLastByte(0x06);

    InMemoryStateView view;
    Account account;
    account.storage[key] = zero;
    view.insert_account(target, account);

    State state(view);
    EthHost host(state, evmc_tx_context{}, EVMC_CANCUN, nullptr, true);

    auto const firstStatus = host.set_storage(target, key, firstNonZero);
    auto const secondStatus = host.set_storage(target, key, secondNonZero);

    BOOST_CHECK_EQUAL(static_cast<int>(firstStatus), static_cast<int>(EVMC_STORAGE_ADDED));
    BOOST_CHECK_EQUAL(static_cast<int>(secondStatus), static_cast<int>(EVMC_STORAGE_ADDED));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
