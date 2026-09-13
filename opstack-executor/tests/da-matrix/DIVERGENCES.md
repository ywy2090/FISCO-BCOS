# da-matrix known_divergence registry

This file registers every **known divergence** between the four sources of the
da-matrix (FISCO / op-geth / op-revm / Solidity GasPriceOracle). It is the
authoritative companion to the `known_divergence` field on grid cases
(`da_matrix.json`).

A divergence is a case where the four ends are **not expected to agree
bit-for-bit**, either because one end deliberately chooses different semantics
in undefined territory, or because the fork is not implemented the same way in
every source. The Task 6 `--check` mode reads the grid's `known_divergence`
field, skips the matching cases, and counts them; a case NOT in this registry is
expected to agree exactly on every source.

## Registry

### `karst_alias` — FISCO `karstConfig` aliases `jovianConfig` (switch_karst)

- **Grid case:** `switch_karst` (already carries `"known_divergence":
  "karst_alias"`).
- **Status:** confirmed consistent across all four ends — but consistent *by
  design*, not because Karst semantics have been verified anywhere.
- **What happens:** FISCO's `karstConfig()` is a placeholder alias of
  `jovianConfig()` (bcos-evm `OpForkSchedule.h`); op-geth selects the Jovian
  operator-fee-fix via `IsJovian` and never consults `KarstTime`; op-revm's
  `KARST` is `>= JOVIAN` so it takes the same `×100` operator path;
  GasPriceOracle has no Karst branch and reports the Jovian formula. All four
  therefore emit the jovian numbers for this row.
- **Why registered:** the agreement is a placeholder coincidence, not evidence
  that real Karst behaviour (Isthmus→Karst DA changes) is implemented. Real
  Karst adaptation is tracked separately (see the da-matrix plan's "单独立案").
- **Op-geth note:** `run_opgeth` keeps `JovianTime=0` for the karst tag because
  op-geth's cost functions key off `IsJovian`/`IsOptimismIsthmus` only.

### `l1_fee_saturation` — FISCO L1 fee saturates at 2^256-1, op-geth is unbounded

- **Grid cases:** none currently (the grid pins slot1/slot7 to ~1e9 so the L1
  fee stays ≈1.4e16, far below 2^256).
- **Status:** registered, NOT triggered.
- **What happens:** FISCO computes the L1 fee in `intx::uint256` and the Fjord
  formula can saturate at `2^256-1` when the base fees are extreme; op-geth uses
  `big.Int` (unbounded). The grid deliberately pins the max-value rows'
  slot1/slot7 to the baseline (1000e6 / 10e6) so this divergence is never
  exercised. If a future grid row raises slot1/slot7 to `≥ 2^200` scale, it
  MUST carry `"known_divergence": "l1_fee_saturation"`.

### `flz_zero_clamp` — flzLen==0 → FISCO 0 vs op-geth clamp to 100

- **Grid cases:** none currently (no zero-byte envelope in the grid).
- **Status:** registered, NOT triggered.
- **What happens:** FISCO's `estimatedDaSizeFromFlz(0)` returns 0 (a documented
  deliberate divergence in `RollupCost.h`), whereas op-geth's
  `estimatedDASizeScaled` floors at `MinTransactionSizeScaled = 100e6` → a
  charged 100-byte minimum. `flz==0` only occurs for a zero-length envelope,
  which is not a real transaction; the grid carries no such envelope, so no
  case hits it. A future grid row with a zero-byte envelope MUST carry
  `"known_divergence": "flz_zero_clamp"`.

## Solidity L1-fee convention note (NOT a divergence)

The Solidity end's `l1_cost` is **not bit-comparable** with the other three
ends by design:

- `GasPriceOracle.getL1Fee(_data)` eats an **unsigned** RLP tx and adds
  `+68` (`flz(data) + 68`, GasPriceOracle.sol:257-258), while FISCO / op-geth /
  op-revm eat the **signed** envelope with no `+68`.
- The Solidity snapshot therefore records `getL1Fee(signedEnvelope)` as a
  **cross-reference only**; the operator fee (`getOperatorFee(gas)`) IS the
  Solidity authority and is directly comparable.
- The grid carries no `known_divergence` for this — it is a convention
  difference in the Solidity snapshot, documented here and in
  `solidity/OperatorFeeCheck.t.sol`.

## `solidity_l1_uint32_overflow` — GasPriceOracle.getL1Fee panics on extreme scalars

- **Grid cases:** `max_isthmus_scalars`, `max_jovian_scalars`,
  `overflow_isthmus`, `overflow_jovian` (all carry `baseFeeScalar ==
  0xffffffff`).
- **Status:** NEW finding from Task 5 four-source comparison — a latent bug in
  the **real Solidity contract**, not in the FISCO implementation.
- **What happens:** `GasPriceOracle._fjordL1Cost` and `_getL1FeeEcotone`
  compute `baseFeeScalar() * 16 * l1BaseFee()` (GasPriceOracle.sol:248, :283).
  `baseFeeScalar()` returns `uint32`, so `baseFeeScalar() * 16` is evaluated in
  uint32 and **reverts (panic: arithmetic overflow)** when
  `baseFeeScalar >= 2**28`. The grid's max rows set scalar `0xffffffff`, so
  `getL1Fee` reverts for those four cases. `getOperatorFee` is **unaffected**
  (isthmus formula uses `Arithmetic.saturatingMul`; jovian formula fits the
  grid's extreme rows in uint256) and remains the Solidity authority.
- **How the snapshot handles it:** `OperatorFeeCheck.t.sol` wraps `getL1Fee` in
  `try/catch` and records the sentinel `0xfff…ff` for the reverting cases. The
  operator fee is recorded normally for all 16 cases.
- **Why not a grid `known_divergence`:** it is a Solidity-implementation bug
  (real contracts never use scalars that large); the FISCO/op-geth/op-revm
  values are correct. Tracked here and in the task-5 report; a follow-up could
  file an upstream note against GasPriceOracle.sol.

## `rpc_l1_fee_scalar_truncation` — RPC `l1FeeScalar` is integer-truncated, op-geth keeps the fractional part

- **Grid cases:** none (this is an RPC wire convention, not a da-matrix cost
  case; the grid compares L1 fee / operator fee, not the receipt scalar).
- **Status:** registered, **intentional** deviation (previously unregistered).
- **What happens (FISCO):** `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp:118-119`
  renders the Bedrock-era `l1FeeScalar` as `raw_scalar / 1e6` using `bcos::u256`
  integer division, which truncates toward zero. It is exact only when the raw
  slot-6 scalar is an exact multiple of `1e6`.
- **What op-geth does:** the same `scalar/1e6` is computed by
  `core/types/rollup_cost.go` `intToScaledFloat` as a `*big.Float`
  (`scalar / 10^6`, 6 decimals) and emitted as the decimal `l1FeeScalar` field
  (`core/types/gen_receipt_json.go:40`), so the fractional part is preserved;
  it is nil from Ecotone on, which FISCO reproduces via the field's presence
  (the meta stores the raw scalar only on the Bedrock formula path).
- **Why deliberate:** FISCO's `opStackMeta` stores the *raw* slot-6 scalar and
  the RPC boundary must emit a canonical integer hex quantity, consistent with
  the rest of `ReceiptResponse`'s `toQuantity` fields. Every corpus and
  canonical Bedrock config uses a `1e6` multiple, so the emitted value matches
  op-geth there; emitting a fractional JSON number for the general case would
  change the field's type away from a hex quantity.
- **Impact:** for any scalar that is **not** a multiple of `1e6`, FISCO's
  `l1FeeScalar` is the truncated integer while op-geth emits the fractional
  value, so the two are not bit-comparable in that case. No grid case or known
  chain config hits it (all use `1e6` multiples), so the da-matrix comparison is
  unaffected.

### `eip7825_deposit_exemption` — op-geth applies the Osaka tx-gas cap to deposits; FISCO's Karst tier exempts them (alignment direction UNRESOLVED)

- **Grid case:** none yet (no da-matrix row exercises a deposit above 2^24); registered
  here because a future differential run (Plan D) against op-geth will surface it.
- **Status: direction unresolved — a previous revision of this entry claimed "op-geth
  deviates from the spec", citing `specs/protocol/karst/overview.md:20`. That citation does
  not exist.** At specs pin `564a0ce` the file is a 464-byte / 16-line stub (TOC plus two
  empty section headings), so there is no line 20; a tree-wide search
  (`grep -rn '7825\|20MGas\|not enabled for deposits' specs/`) hits only `flashblocks.md:615`
  (unrelated), and `git log --all -S'not enabled for deposits'` / `-S'20MGas'` are empty.
  The one real 20M figure is L1/ingress-side resource metering —
  `guaranteed-gas-market.md:48` `MAX_RESOURCE_LIMIT = 20,000,000` — which is not an EL
  transaction-validation rule. **Neither side's alignment is therefore established.**
- **What happens:** FISCO's Karst tier sets `deposit_exempt_from_max_tx_gas = true`
  (`bcos-evm/opstack/OpForkSchedule.cpp:230`), wired at `bcos-evm/opstack/OpTransition.cpp:575`
  as `enforce_max_tx_gas = !cfg.deposit_exempt_from_max_tx_gas`, so an over-cap deposit
  executes normally (pinned by `OpOsakaSemanticsTest.cpp` `DepositExemptFromEip7825MaxGasLimit`,
  status 0 / gasUsed 21000). op-geth `core/state_transition.go:379-383` returns
  `ErrGasLimitTooHigh` when `isOsaka && msg.GasLimit > params.MaxTxGas` inside
  `if (!msg.SkipTransactionChecks)`; deposits do **not** set that flag — the only setter in
  the tree is `internal/ethapi/transaction_args.go:495` (eth_call) — so the cap applies to
  deposits. Note the check sits in `preCheck`, i.e. **before** execution: the
  deposit-tolerant path (`:489-496`, which keeps a failed deposit with `nonce+1`) covers
  errors from the execution phase only and does **not** cover this one, so op-geth's exact
  surface for an over-cap deposit (unprocessable block vs. recorded failure) still needs a
  differential confirmation (Plan D) rather than an assumption.
- **Why it matters / reachability:** FISCO's deposit gas ceiling is 20,000,000 while the
  7825 cap is 2^24 = 16,777,216, so deposits in **(2^24, 20M] are accepted by FISCO and
  rejected by op-geth** — a constructible input (L1-side senders choose deposit gas limits)
  on which the two nodes diverge at that block. No known chain config or corpus vector
  currently emits such a deposit.
- **Disposition (corrected):** FISCO keeps the exemption for now, but it is a
  **FISCO-internal choice** (mirroring the L1 20M guaranteed-gas ceiling), not a
  spec requirement — and op-geth is not thereby "deviating" either. The alignment question
  is **open**: WI-35's earlier verdict is **reopened** (it rested on the fabricated citation)
  and the same correction must reach the workstream that closed its finding against it.
  Resolve with authoritative evidence — a spec statement, an op-geth/op-node PR, or op-node
  code exempting deposits — or align FISCO with op-geth.
- **Review trigger:** any authoritative statement about EIP-7825 and deposits; op-geth
  changing `state_transition.go:379-383` to skip deposits; a Plan D differential run over a
  (2^24, 20M] deposit; or any chain config that lets L1 senders set deposit gas above 2^24.
