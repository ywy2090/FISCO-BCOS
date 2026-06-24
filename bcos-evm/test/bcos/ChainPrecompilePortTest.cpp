#define BOOST_TEST_MODULE ChainPrecompilePortTest

#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos/adapters/InMemoryChainPrecompileAdapter.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{

evmc_address fiscoPrecompileAddress(uint16_t suffix)
{
    evmc_address addr{};
    addr.bytes[18] = static_cast<uint8_t>((suffix >> 8U) & 0xFFU);
    addr.bytes[19] = static_cast<uint8_t>(suffix & 0xFFU);
    return addr;
}

}  // namespace

BOOST_AUTO_TEST_CASE(null_port_returns_before_routing)
{
    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.chainPrecompilePort = nullptr;
    FiscoVmHostPolicy extension(true, std::move(deps));

    evmc_message msg{};
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto result = extension.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(port_dispatch_invoked_for_fisco_address)
{
    bool invoked = false;
    InMemoryChainPrecompileAdapter port(
        [&invoked](
            evmc_revision /*rev*/, evmc_message const& message) -> std::optional<evmc_result> {
            invoked = true;
            evmc_result raw{};
            raw.status_code = EVMC_SUCCESS;
            raw.gas_left = message.gas;
            return raw;
        });

    FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
    deps.chainPrecompilePort = &port;
    FiscoVmHostPolicy extension(true, std::move(deps));

    evmc_message msg{};
    msg.gas = 50'000;
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto result = extension.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(invoked);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

}  // namespace bcos::evm::test
