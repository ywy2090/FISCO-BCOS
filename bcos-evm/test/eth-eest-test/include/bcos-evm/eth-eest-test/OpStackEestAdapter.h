#pragma once

#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <boost/property_tree/ptree.hpp>
#include <map>
#include <string>
#include <vector>

namespace bcos::eest::opstack
{

/// Less-than comparator for evmc_address to use in std::map.
struct AddressLess
{
    bool operator()(evmc_address const& lhs, evmc_address const& rhs) const noexcept
    {
        return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) < 0;
    }
};

/// Holds a single EEST fixture translated to OPStack execution input plus expected post-state.
/// The fixture owns the TestStateView; the caller must keep the fixture alive during execution
/// since OpStackMessageRequest.stateView points into this fixture.
struct OpStackEestFixture
{
    std::string name;
    std::string forkName;
    bcos::evm::reference_tests::TestStateView stateView;
    /// Owned access list storage — input.accessList points here, so the fixture must
    /// outlive the OPStack execution call.
    bcos::evm::Eip2930AccessList storedAccessList;
    bcos::evm::OpStackMessageRequest input;
    /// Expected post-state: address → Account (nonce, balance, code, storage).
    std::map<evmc_address, bcos::evm::state::Account, AddressLess> expectedPost;
    bool expectSuccess{true};
};

/// Convert ONE EEST state test fixture JSON to OPStack execution input.
///
/// \param fixtureJson  A boost property_tree for a single fixture (the "env"/"pre"/"transaction"/
///                     "post" level, not the top-level variant map).
/// \param forkName     e.g. "Cancun", "Prague", "Osaka".
/// \param vm           evmone VM instance.
/// \param hashImpl     Hash implementation (use FakeHash in tests).
OpStackEestFixture adaptStateFixture(boost::property_tree::ptree const& fixtureJson,
    std::string const& forkName, evmc::VM& vm, bcos::crypto::Hash& hashImpl);

/// Map fork name string to evmc_revision.
evmc_revision revisionFromForkName(std::string const& forkName);

/// Map fork name to RevisionConfig.
bcos::evm_standard::RevisionConfig revisionConfigFromForkName(std::string const& forkName);

}  // namespace bcos::eest::opstack
