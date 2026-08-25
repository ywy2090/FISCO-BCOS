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
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

// Karst is NOT independently adapted yet: its execution/receipt behavior is temporarily an alias
// of Jovian (placeholder). Do not treat karstConfig() as a real Karst adaptation. Derived from
// jovianConfig, changing only the fork tag, the same pattern as granite/holocene deriving from
// fjord -- future Jovian changes are automatically carried into Karst, avoiding parallel-literal
// drift.
const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = jovianConfig();
        c.fork = OpFork::Karst;
        return c;
    }();
    return cfg;
}

const OpForkConfig& configAt(const OpForkFlags& flags) noexcept
{
    // decision A5 (feature-flag variant): feature_op_jovian enabled -> Jovian, else Isthmus.
    // Isthmus is the OP-mode baseline; there is no pre-Isthmus config in this minimal loop.
    if (flags.jovianActive)
        return jovianConfig();
    return isthmusConfig();
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
        std::string forkName;
        switch (activation.fork)
        {
        case OpFork::Isthmus:
            forkName = "isthmus";
            break;
        case OpFork::Jovian:
            forkName = "jovian";
            break;
        case OpFork::Karst:
            forkName = "karst";
            break;
        default:
            throw ledger::InvalidOpForkSchedule("unsupported fork in schedule");
        }
        records.push_back(ledger::OpForkActivationRecord{
            .forkName = std::move(forkName),
            .timestamp = activation.timestamp,
        });
    }
    return ledger::canonicalOpForkSchedule(records);
}
}  // namespace bcos::evm::opstack
