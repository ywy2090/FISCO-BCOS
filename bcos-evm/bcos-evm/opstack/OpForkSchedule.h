#pragma once

#include <evmc/evmc.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace bcos::evm::opstack
{
// ────────────────────────────────────────────────────────────────────────────
// OP-Stack fork schedule (Bedrock onward) ↔ Ethereum base fork
//
// Reference: op-geth v1.101702.2 (authority) + optimism docs / specs.
// FB only MODELS Ecotone+ (the enum below): the minimal validator loop is
// Isthmus+-only (decision A5) and the engine -38005 gate rejects pre-Isthmus
// payloads, so Bedrock/Regolith/Canyon are unreachable — they are listed for
// mapping completeness only, NOT implemented.
//
//   OP fork      | Ethereum base | EVM rev (FB)      | FB status
//   -------------+---------------+-------------------+----------------------
//   Bedrock      | London        | —                 | not modeled (unreachable)
//   Regolith     | London        | —                 | not modeled; deposit-tx fixes
//   Canyon       | Shanghai      | —                 | not modeled; EIP-4895/1153/5656/6780
//   Ecotone      | Cancun        | EVMC_CANCUN       | modeled; blob L1 fee (EIP-4844/4788/7516)
//   Fjord        | Cancun        | EVMC_CANCUN       | modeled; FastLZ L1 fee, p256 active
//   Granite      | Cancun        | EVMC_CANCUN       | modeled; 8 precompile size limits
//   Holocene     | Cancun        | EVMC_CANCUN       | modeled; EIP-1559 via 9B extraData
//   Isthmus      | Prague/Pectra | EVMC_PRAGUE       | modeled; EIP-7702/7623/2935/2537 + OP
//                 |               |                   | deposit changes
//   Jovian        | Prague        | EVMC_PRAGUE       | modeled; +DA footprint, operator fee ×100
//   Karst         | Osaka         | EVMC_OSAKA        | modeled; Osaka EVM + Karst precompile caps
//
// Key facts:
//   * Isthmus = all Prague/Pectra features that apply to L2s (optimism docs
//     pectra-changes: "the upcoming Isthmus hardfork will contain all Prague
//     features"); Jovian adds OP-only DA footprint + operator-fee-fix on the
//     same Prague base — hence both map to EVMC_PRAGUE.
//   * Karst maps to EVMC_OSAKA with an independent precompile-override object.
//     Production parse still cannot name `karst` (codec / forkFromName reject it).
//     Tests name Karst via OpForkSchedule::TestBypass.
// ────────────────────────────────────────────────────────────────────────────
enum class OpFork
{
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

struct PrecompileOverrides;

struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
    // When true, runDeposit passes enforce_max_tx_gas=false (EIP-7825 deposit exemption).
    bool deposit_exempt_from_max_tx_gas = false;
    bool has_ecotone_l1_formula;  // true -> Ecotone calldataGas L1; false -> Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;

/// K1 compatibility wrapper: `jovianActive` selects Jovian vs Isthmus when a timestamp
/// schedule is not in use. Timestamp-based selection lives on `OpForkSchedule`
/// (`parse` / `forkAt` / `configAt(uint64_t)`). Injected via OpSchedulerSeam's constructor
/// (same channel as chainId). Isthmus is the OP-mode baseline; `feature_op_jovian`
/// (Features::Flag, genesis [features]) selects Jovian over Isthmus.
struct OpForkFlags
{
    /// feature_op_jovian enabled → Jovian semantics (DA footprint, operator fee ×100,
    /// 17B Jovian extraData); disabled → Isthmus semantics.
    bool jovianActive = false;
};

/// Feature-flag wrapper (decision A5): `jovianActive` -> Jovian, otherwise Isthmus.
/// Timestamp schedules use `OpForkSchedule::configAt(uint64_t)` instead.
const OpForkConfig& configAt(const OpForkFlags& flags) noexcept;

struct OpForkActivation
{
    OpFork fork{};
    uint64_t timestamp{};
};

/// Timestamp schedule: Unix-second activations select Isthmus vs Jovian.
/// Production parse goes through the ledger codec, which cannot name karst.
class OpForkSchedule
{
public:
    static OpForkSchedule parse(std::string_view canonical);
    static OpForkSchedule legacy(bool jovianActive);
    explicit OpForkSchedule(std::vector<OpForkActivation> activations);
    /// Test-only: skip ledger codec validation so tests can name Karst
    /// before the production codec unlocks it.
    struct TestBypass
    {
    };
    OpForkSchedule(std::vector<OpForkActivation> activations, TestBypass);
    [[nodiscard]] OpFork forkAt(uint64_t timestampSeconds) const;
    [[nodiscard]] const OpForkConfig& configAt(uint64_t timestampSeconds) const;

private:
    std::vector<OpForkActivation> m_activations;
};
}  // namespace bcos::evm::opstack
