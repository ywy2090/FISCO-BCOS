#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test::fixtures
{

inline bcos::evm_standard::RevisionConfig revisionConfigFromFixtureRevision(
    std::string const& revision)
{
    bcos::evm_standard::RevisionConfig cfg;
    if (revision == "prague")
    {
        cfg.revision = EVMC_PRAGUE;
        cfg.eip7702 = true;
        cfg.eip7623 = true;
        cfg.eip2537 = true;
        cfg.calldata_floor_per_token = 10;
    }
    else if (revision == "cancun")
    {
        cfg.revision = EVMC_CANCUN;
    }
    else
    {
        BOOST_FAIL("unsupported fixture revision: " << revision);
    }
    cfg.warm_access = true;
    cfg.eip1153 = cfg.revision >= EVMC_CANCUN;
    cfg.eip4844 = cfg.revision >= EVMC_CANCUN;
    cfg.eip5656 = cfg.revision >= EVMC_CANCUN;
    cfg.eip6780 = cfg.revision >= EVMC_CANCUN;
    return cfg;
}

inline ExecuteViaHostInput buildExecuteViaHostInput(FixtureCase const& fixture,
    state::StateView const& stateView, evmc::VM& vm, bcos::crypto::Hash const& hashImpl)
{
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;

    evmc_message msg{};
    msg.kind = fixture.tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.flags = fixture.txProps.isStatic ? EVMC_STATIC : 0;
    msg.gas = fixture.tx.gasLimit;
    msg.sender = fixture.tx.from;
    msg.recipient = fixture.tx.to.value_or(evmc_address{});
    msg.code_address = msg.recipient;
    msg.input_data = fixture.tx.data.data();
    msg.input_size = fixture.tx.data.size();
    msg.value = state::toEvmC(fixture.tx.value);
    input.message = msg;
    input.blockInfo = fixture.block;
    input.gasPrice = fixture.tx.gasPrice;
    input.web3Tx = true;
    input.authorizationListPresent = fixture.authorizationListPresent;
    input.authorizations = fixture.authorizations;
    input.revisionConfig.eth() = revisionConfigFromFixtureRevision(fixture.revision);
    return input;
}

}  // namespace bcos::evm::test::fixtures
