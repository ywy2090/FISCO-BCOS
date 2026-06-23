#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/specs-tests/ExecutionPath.h"
#include "bcos-evm/specs-tests/ForkProfileRegistry.h"
#include "bcos-evm/specs-tests/GeneralStateTestLoader.h"
#include "bcos-task/Task.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm::reference_tests
{

struct ExecutionResult
{
    evmc_status_code status{EVMC_SUCCESS};
    int64_t gasUsed{0};
    bcos::bytes output;
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    std::optional<evmc_bytes32> stateRoot;
    std::optional<evmc_bytes32> logsHash;
    std::optional<std::string> rejectionReason;
};

class PathAdapter
{
public:
    virtual ~PathAdapter() = default;

    virtual ExecutionPath path() const = 0;
    virtual bool supports(ForkProfile const& profile, std::string_view capabilityRowId) const = 0;
    virtual task::Task<ExecutionResult> execute(
        StateTestCase const& testCase, StateSubtest const& subtest) = 0;
};

}  // namespace bcos::evm::reference_tests
