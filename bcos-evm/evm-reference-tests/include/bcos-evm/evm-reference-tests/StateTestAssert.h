#pragma once

#include "bcos-evm/evm-reference-tests/PathAdapter.h"
#include "bcos-evm/evm-reference-tests/StateTestTypes.h"
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
