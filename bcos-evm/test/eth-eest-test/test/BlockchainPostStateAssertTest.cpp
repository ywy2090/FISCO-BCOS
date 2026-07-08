#define BOOST_TEST_MODULE BlockchainPostStateAssertTest
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}
evmc_bytes32 word(uint8_t last)
{
    evmc_bytes32 w{};
    w.bytes[31] = last;
    return w;
}
TestStateView viewWith(evmc_address const& a, state::Account acc)
{
    TestStateView v;
    v.insertAccount(a, std::move(acc));
    return v;
}
}  // namespace

BOOST_AUTO_TEST_CASE(empty_expectation_passes)
{
    TestStateView actual = viewWith(addr(1), {});
    PostStateExpectation exp;  // no hash, no accounts
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(hash_match_and_mismatch)
{
    state::Account acc;
    acc.balance = 500;
    acc.nonce = 1;
    TestStateView actual = viewWith(addr(0x10), acc);

    GstPostStateView gv;
    gv.eip158ClearEmpty = true;
    for (auto const& [a, ac] : actual.accounts())
        gv.accounts.emplace_back(a, ac);
    auto root = computeStateRoot(gv);

    PostStateExpectation good;
    good.hash = root;
    BOOST_CHECK(assertPostState(actual, good, {true}).passed);

    PostStateExpectation bad;
    bad.hash = word(0xff);
    auto rep = assertPostState(actual, bad, {true});
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("postStateHash") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(partial_nonce_only_ignores_balance)
{
    state::Account acc;
    acc.nonce = 7;
    acc.balance = 999;  // intentionally not matched
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 7;  // balance NOT flagged
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(balance_mismatch_fails)
{
    state::Account acc;
    acc.balance = 1;
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasBalance = true;
    e.balance = 2;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("balance") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(code_mismatch_fails)
{
    state::Account acc;
    acc.code = bcos::bytes{0x60, 0x80};
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasCode = true;  // expected empty code
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("code") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(listed_storage_missing_slot_is_zero)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(3));
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasStorage = true;
    e.storage.emplace_back(word(1), word(3));  // matches
    e.storage.emplace_back(word(2), word(0));  // missing ⇒ zero ⇒ ok
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(extra_actual_slot_ignored_in_listed_mode)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(3));
    acc.storage.emplace(word(9), word(9));  // not listed ⇒ ignored
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasStorage = true;
    e.storage.emplace_back(word(1), word(3));
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

BOOST_AUTO_TEST_CASE(empty_storage_object_requires_all_zero)
{
    state::Account nonZero;
    nonZero.storage.emplace(word(1), word(5));
    ExpectedPostAccount e;
    e.hasStorage = true;  // storage: {} ⇒ all slots must be zero
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(!assertPostState(viewWith(addr(1), nonZero), exp).passed);

    state::Account zeroed;
    zeroed.storage.emplace(word(1), word(0));  // explicit zero ⇒ ok
    BOOST_CHECK(assertPostState(viewWith(addr(1), zeroed), exp).passed);
}

BOOST_AUTO_TEST_CASE(storage_key_omitted_skips_storage)
{
    state::Account acc;
    acc.storage.emplace(word(1), word(5));  // present but not checked
    ExpectedPostAccount e;
    e.hasStorage = false;  // storage key omitted
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(viewWith(addr(1), acc), exp).passed);
}

BOOST_AUTO_TEST_CASE(missing_account_fails)
{
    TestStateView actual;  // empty
    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 1;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    auto rep = assertPostState(actual, exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("missing") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(absent_account_pass_and_present_fails)
{
    ExpectedPostAccount e;
    e.kind = ExpectedPostAccount::Kind::Absent;
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);

    TestStateView empty;
    BOOST_CHECK(assertPostState(empty, exp).passed);

    auto rep = assertPostState(viewWith(addr(1), {}), exp);
    BOOST_CHECK(!rep.passed);
    BOOST_CHECK(rep.summary.find("absent") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(presence_only_empty_object_requires_existence)
{
    ExpectedPostAccount e;  // Present, no has* flags
    PostStateExpectation exp;
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(viewWith(addr(1), {}), exp).passed);
    BOOST_CHECK(!assertPostState(TestStateView{}, exp).passed);
}

BOOST_AUTO_TEST_CASE(accounts_take_precedence_over_hash)
{
    // both set: accounts path must be used, wrong hash ignored
    state::Account acc;
    acc.nonce = 3;
    TestStateView actual = viewWith(addr(1), acc);

    ExpectedPostAccount e;
    e.hasNonce = true;
    e.nonce = 3;
    PostStateExpectation exp;
    exp.hash = word(0xff);  // wrong; must be ignored
    exp.accounts.emplace_back(addr(1), e);
    BOOST_CHECK(assertPostState(actual, exp).passed);
}

}  // namespace bcos::evm::reference_tests
