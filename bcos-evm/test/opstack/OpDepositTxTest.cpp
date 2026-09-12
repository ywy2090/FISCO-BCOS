#include <bcos-evm/opstack/OpTransition.h>
#include "TestPrinters.h"
#include <boost/test/unit_test.hpp>
#include <boost/test/tree/decorator.hpp>

using namespace bcos::evm::opstack;
using namespace evmc::literals;

BOOST_AUTO_TEST_SUITE(OpDepositTxSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(ContractCreationHasNullTo, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    DepositTx tx{
        .source_hash = 0x01_bytes32,
        .from = 0x000000000000000000000000000000000000dead_address,
        .to = std::nullopt,  // nullopt = 合约创建
        .mint = intx::uint256{5},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    BOOST_CHECK(!(tx.to.has_value()));
    BOOST_REQUIRE(tx.mint.has_value());
    BOOST_CHECK_EQUAL(*tx.mint, intx::uint256{5});
    BOOST_CHECK(!(tx.is_system_tx));
}

// clang-format off
BOOST_AUTO_TEST_CASE(AbsentMintDiffersFromZero, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    DepositTx tx{};                       // 值初始化：mint 默认 nullopt
    BOOST_CHECK(!(tx.mint.has_value()));    // absent ≠ 0：执行层据此跳过加余额
}

// clang-format off
BOOST_AUTO_TEST_CASE(MintAndValueAreIndependent, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    DepositTx tx{};
    tx.mint = intx::uint256{7};           // mint 有值 → 无条件加 from 余额
    tx.value = intx::uint256{3};          // value → call 中转账（两字段独立）
    BOOST_REQUIRE(tx.mint.has_value());
    BOOST_CHECK_NE(*tx.mint, tx.value);
}

BOOST_AUTO_TEST_SUITE_END()
