#pragma once

#include "bcos-evm/eth-eest-test/PathAdapter.h"
#include "bcos-evm/eth-eest-test/StateTestTypes.h"
#include <string>

namespace bcos::evm::reference_tests
{

struct AssertReport
{
    bool passed{};
    std::string message;
};

AssertReport assertResult(ManifestEntry const& entry, ExpectedPostState const& expected,
    ExecutionResult const& actual, int64_t gasBefore);

}  // namespace bcos::evm::reference_tests
