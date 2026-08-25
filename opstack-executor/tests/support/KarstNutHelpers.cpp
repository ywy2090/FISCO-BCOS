#include "KarstNutHelpers.h"

#include <array>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>
#include <stdexcept>
#include <variant>

namespace opstack_test
{
namespace op = bcos::evm::opstack;

constexpr uint64_t UpgradeDepositSourceDomain = 2;

std::string karstNutQualifiedIntent(size_t index, std::string_view intent)
{
    std::string out = "Karst ";
    out += std::to_string(index);
    out += ": ";
    out.append(intent.data(), intent.size());
    return out;
}

evmc::bytes32 upgradeDepositSourceHash(std::string_view qualifiedIntent)
{
    const auto intentHash = ethash::keccak256(
        reinterpret_cast<uint8_t const*>(qualifiedIntent.data()), qualifiedIntent.size());
    std::array<uint8_t, 64> domainInput{};
    for (size_t i = 0; i < 8; ++i)
        domainInput[24 + i] = static_cast<uint8_t>(UpgradeDepositSourceDomain >> (56 - 8 * i));
    std::memcpy(domainInput.data() + 32, intentHash.bytes, sizeof(intentHash.bytes));
    const auto out = ethash::keccak256(domainInput.data(), domainInput.size());
    evmc::bytes32 hash{};
    std::memcpy(hash.bytes, out.bytes, sizeof(hash.bytes));
    return hash;
}

bool isUpgradeDeposit(op::DepositTx const& dep, std::string_view qualifiedIntent) noexcept
{
    return dep.source_hash == upgradeDepositSourceHash(qualifiedIntent);
}

KarstActivationSegment classifyKarstActivationDeposit(
    op::DepositTx const& dep, std::span<std::string_view const> nutQualifiedIntents)
{
    if (op::isL1AttributesTx(dep))
        return KarstActivationSegment::L1Attributes;
    for (auto intent : nutQualifiedIntents)
    {
        if (isUpgradeDeposit(dep, intent))
            return KarstActivationSegment::NutUpgrade;
    }
    return KarstActivationSegment::UserDeposit;
}

void validateKarstActivationOrder(
    std::span<op::OpBlockTx const> txs, std::span<std::string_view const> nutQualifiedIntents)
{
    if (txs.empty())
        throw std::runtime_error("Karst activation block is empty");
    enum class Phase
    {
        NeedL1,
        UserOrNut,
        NutOnly,
    };
    Phase phase = Phase::NeedL1;
    size_t nutIndex = 0;
    for (auto const& btx : txs)
    {
        if (!std::holds_alternative<op::DepositTx>(btx.tx))
            throw std::runtime_error(
                "Karst activation block must not contain ordinary transactions");
        auto const& dep = std::get<op::DepositTx>(btx.tx);
        auto const segment = classifyKarstActivationDeposit(dep, nutQualifiedIntents);
        switch (segment)
        {
        case KarstActivationSegment::L1Attributes:
            if (phase != Phase::NeedL1)
                throw std::runtime_error(
                    "L1 attributes deposit must be first in Karst activation block");
            phase = Phase::UserOrNut;
            break;
        case KarstActivationSegment::UserDeposit:
            if (phase == Phase::NeedL1)
                throw std::runtime_error(
                    "user deposit before L1 attributes in Karst activation block");
            if (phase == Phase::NutOnly)
                throw std::runtime_error(
                    "user deposit after NUT deposits in Karst activation block");
            break;
        case KarstActivationSegment::NutUpgrade:
            if (phase == Phase::NeedL1)
                throw std::runtime_error(
                    "NUT deposit before L1 attributes in Karst activation block");
            phase = Phase::NutOnly;
            if (nutIndex >= nutQualifiedIntents.size())
                throw std::runtime_error("unexpected extra NUT deposit");
            if (!isUpgradeDeposit(dep, nutQualifiedIntents[nutIndex]))
                throw std::runtime_error("NUT deposit out of order or wrong source hash");
            ++nutIndex;
            break;
        }
    }
    if (phase == Phase::NeedL1)
        throw std::runtime_error("Karst activation block missing L1 attributes deposit");
    if (nutIndex != nutQualifiedIntents.size())
        throw std::runtime_error("Karst activation block missing NUT deposits");
}
}  // namespace opstack_test
