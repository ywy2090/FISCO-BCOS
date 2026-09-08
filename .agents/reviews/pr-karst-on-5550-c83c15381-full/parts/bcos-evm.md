# bcos-evm module — full @ c83c15381

**Files:** `eth/state/state.{hpp,cpp}`, `opstack/OpForkSchedule.{h,cpp}`, `OpPrecompiles.{h,cpp}`, `OpTransition.{h,cpp}`, tests `OpForkSchedule{,Codec}Test`, `OpOsakaSemanticsTest`, `OpPrecompilesTest`, `support/{KarstScheduleFixtures,OpForkFlagsCompat}.h`.

## Claim-audit

Karst live config is Osaka + 7825 deposit exemption via flag, as claimed:
`karstConfig().rev = EVMC_OSAKA`, `deposit_exempt_from_max_tx_gas = true`; `runDeposit` passes `{.enforce_max_tx_gas = !cfg.deposit_exempt_from_max_tx_gas}`; `validate_transaction` gates `MAX_TX_GAS_LIMIT` on `policy.enforce_max_tx_gas && rev >= EVMC_OSAKA`. Ordinary txs keep default `policy = {}` (enforce on).

Additional live Karst behaviour **named in `OpForkSchedule.h` (not hidden):** independent precompile table (P256 gas 6900, bn256 pairing 57600/300 pairs; BLS caps stay Jovian). Treat as disclosed-in-module, not a description miss.

## Findings

Zero new findings. Not re-filed: F1–F14, carry-forward LOWs (`kDepositTxType`/`kP256*` naming, op-geth header citations, TestBypass, unused Osaka `expected.bin`).

## Checked, and not a problem

1. **7825 policy wiring (block / eth_call / deposit).** Default on; dry-run must pass false; deposits invert the Karst flag.

```168:172:bcos-evm/bcos-evm/eth/state/state.hpp
struct TxValidationPolicy
{
    /// When false, skip the Osaka EIP-7825 per-tx gas cap. OP deposits set this
    /// from `deposit_exempt_from_max_tx_gas` so derivation cannot brick.
    bool enforce_max_tx_gas = true;
```

```385:386:bcos-evm/bcos-evm/eth/state/state.cpp
    if (policy.enforce_max_tx_gas && rev >= EVMC_OSAKA && tx.gas_limit > MAX_TX_GAS_LIMIT)
        return make_error_code(MAX_GAS_LIMIT_EXCEEDED);
```

```529:531:bcos-evm/bcos-evm/opstack/OpTransition.cpp
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(maskedView, validateBlock, tx, cfg.rev,
        blockGasLeft, 0, {.enforce_max_tx_gas = !cfg.deposit_exempt_from_max_tx_gas});
```

`opValidate` forwards `policy` unchanged into `validate_transaction` (line 397). Tests: `KarstOrdinaryTxRejectsGasOverEip7825Cap`, `KarstEthCallSkipsEip7825MaxGasLimit`, `DepositExemptFromEip7825MaxGasLimit`.

2. **No leftover policy half-fix.** `TxValidationPolicy` has one field; blob `MAX_TX_BLOB_COUNT` stays always-on at Osaka (geth #32641 is the gas cap only). OP still whitelist-rejects `blob` and `type > set_code` before that check.

3. **Karst ≠ Jovian alias.** `ensureKarstIsOsaka` + tests `KarstConfigIsOsakaNotJovianAlias` / `KarstImpliesOsakaConfig`. Precompile pointer `karstConfig().precompiles != jovianConfig().precompiles`.

4. **Precompile table symmetry.** Length caps use `gas_cost_override = -1`; P256 uses `max_input_size = 0` (OpHost: limit only if `> 0`). Karst 0x08 = 300×192 = 57600, pinned by `KarstBn256PairingCapsAt300Pairs`.

5. **`OpForkFlagsCompat` cannot select Karst** — test-only, documented; production is timestamp `configAt`. `karstOnly()` uses `TestBypass` but still returns `karstConfig()` via `configAt(2)`.

## Conventions

- `state.{hpp,cpp}`: evmone Apache/SPDX (vendored). Opstack/test files in this slice have no FISCO grant — same pre-existing opstack pattern; `k*` tables/`kDepositTxType` vs `c_l1InfoDepositGas` unchanged. `throw std::runtime_error` / `InconsistentExecutionConfig : logic_error` pre-existing, not `BOOST_THROW_EXCEPTION`.

## Could not determine

Live `eth_call` caller passing `{.enforce_max_tx_gas = false}` sits outside this slice (executor). Known-items already record `enforce_max_tx_gas = !call`. Minimal read: executor `call=true` site.

Reviewed with fisco-review v1.20.1
