#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>

namespace bcos::evm::opstack
{
namespace
{
OpFork forkFromName(std::string_view forkName)
{
    if (forkName == "isthmus")
        return OpFork::Isthmus;
    if (forkName == "jovian")
        return OpFork::Jovian;
    if (forkName == "karst")
        return OpFork::Karst;
    throw ledger::InvalidOpForkSchedule("unknown fork");
}

const OpForkConfig& configForFork(OpFork fork)
{
    switch (fork)
    {
    case OpFork::Isthmus:
        return isthmusConfig();
    case OpFork::Jovian:
        return jovianConfig();
    case OpFork::Karst:
        return karstConfig();
    default:
        throw ledger::InvalidOpForkSchedule("unsupported fork config");
    }
}

std::string forkNameFromEnum(OpFork fork)
{
    switch (fork)
    {
    case OpFork::Isthmus:
        return "isthmus";
    case OpFork::Jovian:
        return "jovian";
    case OpFork::Karst:
        return "karst";
    default:
        throw ledger::InvalidOpForkSchedule("unknown or pre-Isthmus fork");
    }
}

void validateActivations(std::span<const OpForkActivation> activations)
{
    std::vector<ledger::OpForkActivationRecord> records;
    records.reserve(activations.size());
    for (const auto& activation : activations)
    {
        records.push_back(ledger::OpForkActivationRecord{
            .forkName = forkNameFromEnum(activation.fork),
            .timestamp = activation.timestamp,
        });
    }
    ledger::detail::validateScheduleRecords(records);
}
}  // namespace

const OpForkConfig& ecotoneConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Ecotone,
        .rev = EVMC_CANCUN,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .deposit_exempt_from_max_tx_gas = false,
        .has_ecotone_l1_formula = true,
    };
    return cfg;
}

const OpForkConfig& fjordConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Fjord,
        .rev = EVMC_CANCUN,
        .precompiles = &fjordPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .deposit_exempt_from_max_tx_gas = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& graniteConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Granite;
        c.precompiles = &granitePrecompileOverrides();
        return c;
    }();
    return cfg;
}

const OpForkConfig& holoceneConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Holocene;
        c.precompiles = &granitePrecompileOverrides();
        return c;
    }();
    return cfg;
}

const OpForkConfig& isthmusConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Isthmus,
        .rev = EVMC_PRAGUE,
        .precompiles = &isthmusPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .deposit_exempt_from_max_tx_gas = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& jovianConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Jovian,
        .rev = EVMC_PRAGUE,
        .precompiles = &jovianPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
        .deposit_exempt_from_max_tx_gas = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Karst,
        .rev = EVMC_OSAKA,
        .precompiles = &karstPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
        .deposit_exempt_from_max_tx_gas = true,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

OpForkSchedule OpForkSchedule::parse(std::string_view canonical)
{
    const auto records = ledger::parseOpForkSchedule(canonical);
    std::vector<OpForkActivation> activations;
    activations.reserve(records.size());
    for (const auto& record : records)
    {
        activations.push_back(OpForkActivation{
            .fork = forkFromName(record.forkName),
            .timestamp = record.timestamp,
        });
    }
    return OpForkSchedule(std::move(activations));
}

OpForkSchedule OpForkSchedule::legacy(bool jovianActive)
{
    return OpForkSchedule(std::vector<OpForkActivation>{OpForkActivation{
        .fork = jovianActive ? OpFork::Jovian : OpFork::Isthmus,
        .timestamp = 0,
    }});
}

OpForkSchedule::OpForkSchedule(std::vector<OpForkActivation> activations)
  : m_activations(std::move(activations))
{
    validateActivations(m_activations);
}

OpForkSchedule::OpForkSchedule(std::vector<OpForkActivation> activations, TestBypass)
  : m_activations(std::move(activations))
{}

OpFork OpForkSchedule::forkAt(uint64_t timestampSeconds) const
{
    OpFork activeFork = m_activations.front().fork;
    for (const auto& activation : m_activations)
    {
        if (activation.timestamp > timestampSeconds)
            break;
        activeFork = activation.fork;
    }
    return activeFork;
}

uint64_t OpForkSchedule::baselineTimestamp() const
{
    return m_activations.front().timestamp;
}

const OpForkConfig& OpForkSchedule::configAt(uint64_t timestampSeconds) const
{
    return configForFork(forkAt(timestampSeconds));
}

std::string OpForkSchedule::canonicalString() const
{
    std::vector<ledger::OpForkActivationRecord> records;
    records.reserve(m_activations.size());
    for (const auto& activation : m_activations)
    {
        records.push_back(ledger::OpForkActivationRecord{
            .forkName = forkNameFromEnum(activation.fork),
            .timestamp = activation.timestamp,
        });
    }
    return ledger::detail::serializeScheduleRecords(records);
}
}  // namespace bcos::evm::opstack
