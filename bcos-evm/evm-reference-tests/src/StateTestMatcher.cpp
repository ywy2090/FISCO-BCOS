#include "bcos-evm/evm-reference-tests/StateTestMatcher.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace bcos::evm::reference_tests
{
namespace
{
namespace pt = boost::property_tree;

[[noreturn]] void throwMatcherError(std::string const& message)
{
    throw std::runtime_error(message);
}

ExecutionPath parsePath(std::string const& value)
{
    if (value == "Reference")
    {
        return ExecutionPath::Reference;
    }
    if (value == "BcosBaseline")
    {
        return ExecutionPath::BcosBaseline;
    }
    if (value == "OpStackBaseline")
    {
        return ExecutionPath::OpStackBaseline;
    }
    throwMatcherError("Unknown expectations path: " + value);
}

MatchDecision::Kind parseKind(std::string const& value)
{
    if (value == "Run")
    {
        return MatchDecision::Kind::Run;
    }
    if (value == "Skip")
    {
        return MatchDecision::Kind::Skip;
    }
    if (value == "KnownDiff")
    {
        return MatchDecision::Kind::KnownDiff;
    }
    if (value == "Deviation")
    {
        return MatchDecision::Kind::Deviation;
    }
    throwMatcherError("Unknown expectations kind: " + value);
}

std::vector<std::string> parseStringArray(pt::ptree const& node, char const* field)
{
    std::vector<std::string> values;
    if (auto const child = node.get_child_optional(field))
    {
        for (auto const& [key, item] : *child)
        {
            static_cast<void>(key);
            values.push_back(item.get_value<std::string>());
        }
    }
    return values;
}

bool matchesHardSkipPattern(std::string const& caseId)
{
    static std::vector<std::regex> const patterns = {
        std::regex(R"(^stTimeConsuming/)"),
        std::regex(R"(.*vmPerformance/loop.*)"),
        std::regex(R"(^stEOF/)"),
        std::regex(R"(^stStaticCall/static_Call1MB)"),
        std::regex(R"(RevertInCreateInInit)"),
        std::regex(R"(InitCollisionParis)"),
        std::regex(R"(dynamicAccountOverwriteEmpty_Paris)"),
        std::regex(R"(create2collisionStorageParis)"),
    };

    std::string const prefix = "GeneralStateTests/";
    auto const normalized = caseId.rfind(prefix, 0) == 0 ? caseId.substr(prefix.size()) : caseId;
    for (auto const& pattern : patterns)
    {
        if (std::regex_search(normalized, pattern) || std::regex_search(caseId, pattern))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

StateTestMatcher::StateTestMatcher(std::filesystem::path expectationsJson)
{
    std::ifstream input(expectationsJson);
    if (!input.good())
    {
        throwMatcherError("Failed to open expectations: " + expectationsJson.string());
    }

    pt::ptree tree;
    pt::read_json(input, tree);
    if (!tree.get_child_optional("expectations"))
    {
        return;
    }

    for (auto const& [key, node] : tree.get_child("expectations"))
    {
        static_cast<void>(key);
        ExpectationRule rule;
        rule.caseId = node.get<std::string>("caseId");
        rule.path = parsePath(node.get<std::string>("path"));
        rule.kind = parseKind(node.get<std::string>("kind"));
        if (auto const reason = node.get_optional<std::string>("reason"))
        {
            rule.reason = *reason;
        }
        rule.affectedFields = parseStringArray(node, "affectedFields");
        m_rules.push_back(std::move(rule));
    }
}

MatchDecision StateTestMatcher::decide(std::string const& caseId, ExecutionPath path) const
{
    for (auto const& rule : m_rules)
    {
        if (rule.caseId == caseId && rule.path == path)
        {
            return MatchDecision{rule.kind, rule.reason, rule.affectedFields};
        }
    }

    if (matchesHardSkipPattern(caseId))
    {
        return MatchDecision{MatchDecision::Kind::Skip, std::string("geth hard skip pattern"), {}};
    }

    return MatchDecision{};
}

}  // namespace bcos::evm::reference_tests
