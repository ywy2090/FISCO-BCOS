#define BOOST_TEST_MODULE FiscoSstoreStatusTest

#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <vector>

namespace bcos::evm::test
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

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FiscoSstoreStatusTest)

BOOST_AUTO_TEST_CASE(legacy_off_sstore_status_matrix)
{
    struct Case
    {
        bool fixStorageStatus;
        bool existingIsZero;
        bool newIsZero;
        evmc_storage_status expected;
    };

    std::vector<Case> const cases = {
        {false, true, true, EVMC_STORAGE_ASSIGNED},
        {false, false, true, EVMC_STORAGE_DELETED},
        {false, true, false, EVMC_STORAGE_MODIFIED},
        {false, false, false, EVMC_STORAGE_ASSIGNED},
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
            state::test::InMemoryStateView view;
            state::Account account;
            if (!testCase.existingIsZero)
            {
                account.storage[key] = nonZero;
            }
            view.insert_account(target, account);

            state::State state(view);
            evmc::VM vm{evmc_create_evmone()};
            bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN, .eip2929 = true};

            FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
            deps.state = &state;
            deps.revisionFlags.fix_storage_status = testCase.fixStorageStatus;
            FiscoVmHostPolicy policy(false, std::move(deps));

            state::EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), &policy);

            auto const newValue = testCase.newIsZero ? zero : nonZero;
            auto const status = host.set_storage(target, key, newValue);

            BOOST_CHECK_EQUAL(static_cast<int>(status), static_cast<int>(testCase.expected));
            BOOST_CHECK(state::Bytes32Equal{}(state.get_storage(target, key), newValue));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
