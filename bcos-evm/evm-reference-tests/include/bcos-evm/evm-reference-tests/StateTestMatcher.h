#pragma once

#include "bcos-evm/evm-reference-tests/ExecutionPath.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

struct MatchDecision
{
    enum class Kind
    {
        Run,
        Skip,
        KnownDiff,
        Deviation
    };

    Kind kind{Kind::Run};
    std::optional<std::string> reason;
    std::vector<std::string> affectedFields;
};

class StateTestMatcher
{
public:
    explicit StateTestMatcher(std::filesystem::path expectationsJson);

    MatchDecision decide(std::string const& caseId, ExecutionPath path) const;

private:
    struct ExpectationRule
    {
        std::string caseId;
        ExecutionPath path{};
        MatchDecision::Kind kind{MatchDecision::Kind::Run};
        std::optional<std::string> reason;
        std::vector<std::string> affectedFields;
    };

    std::vector<ExpectationRule> m_rules;
};

}  // namespace bcos::evm::reference_tests
