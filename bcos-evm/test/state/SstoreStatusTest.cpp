#define BOOST_TEST_MODULE SstoreStatusTest
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
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

BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SstoreStatusTest)

BOOST_AUTO_TEST_CASE(ethereum_precise_sstore_status_matrix)
{
    struct Case
    {
        bool existingIsZero;
        bool newIsZero;
        evmc_storage_status expected;
    };

    std::vector<Case> const cases = {
        {true, true, EVMC_STORAGE_ASSIGNED},
        {false, true, EVMC_STORAGE_DELETED},
        {true, false, EVMC_STORAGE_ADDED},
        {false, false, EVMC_STORAGE_ASSIGNED},
    };

    auto const target = addressFromLastByte(0x33);
    auto const key = valueFromLastByte(0x01);
    auto const nonZero = valueFromLastByte(0x09);
    auto const zero = evmc_bytes32{};

    for (auto const& testCase : cases)
    {
        BOOST_TEST_CONTEXT(
            "existingIsZero=" << testCase.existingIsZero << ", newIsZero=" << testCase.newIsZero)
        {
            InMemoryStateView view;
            Account account;
            if (!testCase.existingIsZero)
            {
                account.storage[key] = nonZero;
            }
            view.insert_account(target, account);

            State state(view);
            evmc::VM vm{evmc_create_evmone()};
            bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .warm_access = true};
            EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), nullptr);

            auto const newValue = testCase.newIsZero ? zero : nonZero;
            auto const status = host.set_storage(target, key, newValue);

            BOOST_CHECK_EQUAL(static_cast<int>(status), static_cast<int>(testCase.expected));
            BOOST_CHECK(Bytes32Equal{}(state.get_storage(target, key), newValue));
        }
    }
}

BOOST_AUTO_TEST_CASE(noop_sstore_returns_assigned_when_current_equals_value)
{
    auto const target = addressFromLastByte(0x55);
    auto const key = valueFromLastByte(0x03);
    auto const one = valueFromLastByte(0x01);

    InMemoryStateView view;
    Account account;
    view.insert_account(target, account);

    State state(view);
    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_OSAKA, .warm_access = true};
    EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), nullptr);

    BOOST_CHECK_EQUAL(
        static_cast<int>(host.set_storage(target, key, one)), static_cast<int>(EVMC_STORAGE_ADDED));
    BOOST_CHECK_EQUAL(static_cast<int>(host.set_storage(target, key, one)),
        static_cast<int>(EVMC_STORAGE_ASSIGNED));
}

BOOST_AUTO_TEST_CASE(precise_uses_original_committed_value_for_status)
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
    evmc::VM vm{evmc_create_evmone()};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .warm_access = true};
    EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), nullptr);

    auto const firstStatus = host.set_storage(target, key, firstNonZero);
    auto const secondStatus = host.set_storage(target, key, secondNonZero);

    BOOST_CHECK_EQUAL(static_cast<int>(firstStatus), static_cast<int>(EVMC_STORAGE_ADDED));
    BOOST_CHECK_EQUAL(static_cast<int>(secondStatus), static_cast<int>(EVMC_STORAGE_ASSIGNED));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
