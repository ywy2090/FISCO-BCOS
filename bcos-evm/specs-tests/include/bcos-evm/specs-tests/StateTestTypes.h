#pragma once

#include "bcos-evm/specs-tests/EvidenceKind.h"
#include "bcos-evm/specs-tests/ExecutionPath.h"
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

struct StateSubtest
{
    std::string fork;
    int dataIndex{};
    int gasIndex{};
    int valueIndex{};
};

struct ExpectedPostState
{
    std::optional<std::string> expectException;
    std::optional<evmc_bytes32> stateRoot;
    std::optional<evmc_bytes32> logsHash;
    // transitional fields:
    std::optional<evmc_status_code> status;
    std::optional<int64_t> gasUsed;
    int dataIndex{};
    int gasIndex{};
    int valueIndex{};
};

struct ManifestEntry
{
    std::string evidenceId;
    std::string sourceSuite;
    std::string casePath;
    std::optional<std::string> variantKey;
    std::string forkProfileId;
    ExecutionPath path{};
    EvidenceKind evidenceKind{};
    std::vector<std::string> capabilityRowIds;
    std::vector<std::string> assertLevels;
    /// When upstream GST JSON lacks post for profile fork (e.g. Osaka), use this fork's post.
    std::optional<std::string> postFork;
};

}  // namespace bcos::evm::reference_tests
