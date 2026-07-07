#pragma once
#include <string_view>

namespace bcos::evm::reference_tests
{
/// evmone statetest slow patterns + EEST native slow cases.
/// Full nightly run: pass `--gtest_filter=*` on CLI to override.
inline constexpr std::string_view kEestGranularDefaultGtestFilter =
    "-"
    // evmone legacy GST (still present in some imported dirs)
    "stCreateTest.CreateOOGafterMaxCodesize:"
    "stQuadraticComplexityTest.Call50000_sha256:"
    "stTimeConsuming.static_Call50000_sha256:"
    "stTimeConsuming.CALLBlake2f_MaxRounds:"
    "VMTests/vmPerformance.*:"
    // EEST native slow / unbounded loops
    "*run_until_out_of_gas*:"
    "*test_run_until_out_of_gas*:"
    "*sha256*50000*:"
    "*CALLBlake2f*MaxRounds*";
}  // namespace bcos::evm::reference_tests
