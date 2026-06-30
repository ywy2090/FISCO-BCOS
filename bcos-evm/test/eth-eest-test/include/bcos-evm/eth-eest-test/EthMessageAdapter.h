#pragma once

#include "bcos-evm/eth-eest-test/PathAdapter.h"
#include <evmc/evmc.hpp>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm::reference_tests
{

class EthReferenceExecuteAdapter : public PathAdapter
{
public:
    EthReferenceExecuteAdapter(
        ForkProfile profile, bcos::crypto::Hash& hashImpl, evmc::VM& vm) noexcept;

    ExecutionPath path() const override { return ExecutionPath::Reference; }

    bool supports(ForkProfile const& profile, std::string_view capabilityRowId) const override;

    task::Task<ExecutionResult> execute(
        StateTestCase const& testCase, StateSubtest const& subtest) override;

private:
    ForkProfile m_profile;
    bcos::crypto::Hash* m_hashImpl{};
    evmc::VM* m_vm{};
};

}  // namespace bcos::evm::reference_tests
