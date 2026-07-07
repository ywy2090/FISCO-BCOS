#pragma once

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include <boost/property_tree/ptree.hpp>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
/// Parse every test object in an EEST blockchain fixture ptree.
/// Skips engine-only objects (no `pre` + `genesisBlockHeader`).
std::vector<BlockchainTest> loadBlockchainTests(boost::property_tree::ptree const& root);

TestBlockHeader parseBlockHeader(boost::property_tree::ptree const& j);
TestBlock parseTestBlock(boost::property_tree::ptree const& j, std::string_view network);
}  // namespace bcos::evm::reference_tests
