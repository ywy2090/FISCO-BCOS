# opt8n — OP-Stack t8n vector generator

`main.go` is a standalone Go program, **not** part of the FISCO-BCOS build.
Its execution loop is copied from upstream op-geth's
`cmd/evm/internal/t8ntool/execution.go` (`(*Prestate).Apply`), with the
substitutions listed below. It is the ground truth for the
`bcos-evm/test/opstack/t8n/vectors/*.json` differential-test vectors: every
`postState` and `_op_expected` field in those files was produced by this
program executing against a real op-geth EVM + StateDB, never hand-typed
(see the plan's "预注册的 gate 纪律", rule 2).

Reference: `bcos-evm/docs/superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md`
("向量 JSON 格式（v2）" section defines the output schema this program emits).

## Why this can't just import t8ntool

`cmd/evm/internal/t8ntool` is an `internal` package; Go only allows
importing it from within `cmd/evm/...`. `cmd/opt8n` is a sibling of
`cmd/evm`, not a descendant, so the loop had to be re-implemented rather than
imported. It has also been trimmed to what the OP-Stack vector matrix
actually needs: no blob txs, no EIP-6110/7002/7251 requests (both are
no-ops once Isthmus is active anyway — see `chainConfig.IsIsthmus` checks in
the original), no verkle/binary trie, no mining reward/ommers/withdrawals,
single block per vector file.

## Substitution 1: chainConfig assembly

There is no `tests.GetChainConfig("isthmus")` upstream — that helper only
knows plain-Ethereum fork names. Per the plan, the first step was to grep
the OP fork field names before writing any assembly code:

```
$ grep -rn "IsthmusTime\|RegolithTime\|Optimism" params/config.go | head -20
...
386:	OptimismTestConfig = func() *ChainConfig {
387:		conf := *MergedTestChainConfig // copy the config
388:		conf.BlobScheduleConfig = nil
389:		conf.OsakaTime = nil
390:		conf.BedrockBlock = big.NewInt(0)
391:		zero := uint64(0)
392:		conf.RegolithTime = &zero
393:		conf.CanyonTime = &zero
394:		conf.EcotoneTime = &zero
395:		conf.FjordTime = &zero
396:		conf.GraniteTime = &zero
397:		conf.HoloceneTime = &zero
398:		conf.IsthmusTime = &zero
399:		conf.JovianTime = &zero
400:		conf.KarstTime = nil
401:		conf.InteropTime = nil
402:		conf.Optimism = &OptimismConfig{EIP1559Elasticity: 6, EIP1559Denominator: 50, EIP1559DenominatorCanyon: uint64ptr(250)}
403:		return &conf
404:	}()
```

`params.OptimismTestConfig` already exists upstream and does exactly what
the plan asked for (fork times all pinned to 0, `Optimism` populated) — so
rather than re-deriving `AllCliqueProtocolChanges`/`MergedTestChainConfig`
by hand, `buildChainConfig()` copies this struct by value and only
re-points the one field that differs per `--fork`:

```go
func buildChainConfig(fork string) (*params.ChainConfig, error) {
	conf := *params.OptimismTestConfig // struct copy; pointer fields still
	                                    // point at OptimismTestConfig's own
	                                    // *uint64s, but we only ever REASSIGN
	                                    // those pointer fields below, never
	                                    // mutate through them, so the shared
	                                    // package var is never corrupted.
	conf.ChainID = big.NewInt(8453) // Base mainnet, matches the plan's v2 example

	switch fork {
	case "isthmus":
		conf.JovianTime = nil // OptimismTestConfig defaults this to 0; disable it
	case "jovian":
		// OptimismTestConfig already sets JovianTime = 0.
	default:
		return nil, fmt.Errorf("unknown --fork %q (want isthmus|jovian)", fork)
	}
	return &conf, nil
}
```

`OptimismConfig` fields (confirmed via `params/config_op.go`):
`EIP1559Elasticity uint64`, `EIP1559Denominator uint64`,
`EIP1559DenominatorCanyon *uint64` — all already set by `OptimismTestConfig`,
untouched here.

## Substitution 2: tx parsing (two forms) + OP-Stack L1Cost/OperatorCost wiring

`txs[]` entries are a tagged union on `_op_type`:

- `"_op_type": "deposit"` + `_op_deposit: {from, to, mint, value, is_system_tx,
  source_hash}` → `types.NewTx(&types.DepositTx{...})`.
- anything else (default `"eip1559"`) → either `_op_raw` (decoded via
  `tx.UnmarshalBinary`, already-signed passthrough) or field values +
  `secretKey` (the generator signs with `types.SignNewTx` and writes the
  resulting `_op_raw` + `v`/`r`/`s` back into the output vector — the v2
  schema's strengthening #1, "`_op_raw` is the authoritative envelope").

Deposit note: post-Regolith, `IsSystemTransaction` **must be `false`** —
`state_transition.go`'s `preCheck()` rejects `IsSystemTx=true` once
`IsOptimismRegolith` is active (`ErrSystemTxNotSupported`). The historical
"system tx, gas-free" special case for the L1 attributes deposit was retired
by Regolith; all vectors targeting Isthmus/Jovian must set this field false.

Vanilla `t8ntool`'s `Apply()` builds its `vm.BlockContext` by hand and never
sets `L1CostFunc`/`OperatorCostFunc` (there's no L1Block predeploy in plain
`ethereum/tests` vectors). This generator mirrors `core/evm.go`'s
`NewEVMBlockContext` instead:

```go
vmContext.L1CostFunc = types.NewL1CostFunc(chainConfig, statedb)
if chainConfig.IsOptimismIsthmus(blockTime) {
	vmContext.OperatorCostFunc = types.NewOperatorCostFunc(chainConfig, statedb)
}
```

Both are lazy closures over the *mutable* `statedb`, evaluated once per
block on first call and cached — consensus-critical detail from
`rollup_cost.go`'s own comment: they must read state **after** any L1
attributes deposit in the block has written it, which is naturally satisfied
here since these closures are only invoked when the first non-deposit tx's
`buyGas()` runs.

Deposit receipt fields (`DepositNonce`, `DepositReceiptVersion`) require
**no extra code** — `core.ApplyTransactionWithEVM` → `MakeReceipt` already
populates them for any Regolith+/Canyon+ deposit tx; the generator just
reads `receipt.DepositNonce`/`receipt.DepositReceiptVersion` back out.

`receipt.L1Fee` (exported as `_op_expected.receipts[i]._op_l1_fee`) is a
**separate, informational** derivation: `types.Receipts.DeriveFields` (only
triggered when a block has ≥2 txs, i.e. an L1 attributes deposit + ≥1 user
tx) re-parses `txs[0].Data()` — the deposit's own calldata — independently
of the state-driven L1 cost that was actually charged during execution. For
`isthmus_transfer_basic` these two numbers are made to agree on purpose (see
"L1Block predeploy" below); they need not agree in general (e.g. a vector
whose deposit calldata scalars intentionally differ from the state it seeds
would be a legitimate way to test that op-stack reads from state, not
calldata — out of scope for Task 1).

## Two bugs found while building the first vector (fixed, documented so
## Task 3/4 authors don't rediscover them)

1. **`math.HexOrDecimal256` value fields silently marshal to `{}`.** Its
   `MarshalText`/`UnmarshalJSON` are defined on a *pointer* receiver, and the
   underlying `big.Int` has zero exported fields. A struct field declared as
   the bare (non-pointer) type is not addressable when the struct is passed
   to `json.Marshal` by value, so `encoding/json` cannot promote to the
   pointer method set and falls back to reflecting the (field-less)
   struct — producing `"maxFeePerGas": {}` instead of a hex string, with no
   error. Every `HexOrDecimal256` field in `outputSignedTx` must be a
   pointer (`*math.HexOrDecimal256`). `HexOrDecimal64` has no such problem
   (`MarshalText` has a value receiver, since it wraps a plain `uint64`).

2. **Omitting `env.currentRandom` silently disables every OP-Stack fee-vault
   credit.** `vm.NewEVM` computes `chainRules = chainConfig.Rules(number,
   blockCtx.Random != nil, time)`, and `Rules.IsOptimismBedrock = isMerge &&
   config.IsOptimismBedrock(num)`. Without `Random` set, `isMerge` is false,
   so the entire `if optimismConfig != nil && rules.IsOptimismBedrock &&
   !IsDepositTx { ... }` block in `state_transition.go` (BaseFeeVault,
   L1FeeVault, OperatorFeeVault crediting) is skipped — with **no error**,
   only a receipt/postState that looks plausible but is missing three
   `AddBalance` calls. First generation run produced a `postState` with only
   coinbase's plain EIP-1559 tip and no vault entries at all; adding
   `"currentRandom": "0x1"` (any non-null value) to `env` fixed it. Every OP-
   Stack vector's `env` **must** set `currentRandom`.

## Building

```bash
cp -r bcos-evm/test/opstack/t8n/generator /Users/octopus/octo/code/blockchain-impl/op-geth/cmd/opt8n
cd /Users/octopus/octo/code/blockchain-impl/op-geth
go build ./cmd/opt8n
# binary: /Users/octopus/octo/code/blockchain-impl/op-geth/opt8n
```

op-geth checkout pin: **v1.101702.2**, same tag as
`bcos-evm/test/eth-eest-test/assets/upstream-pins.json`. Commit at time of
this generation:

```
e8800cffe53d459cde8a07c8e8f1de9d86e79e07
```

recorded automatically into every output file's `_op_test_vectors.generator_commit`
via `--op-geth-commit "$(git -C <op-geth checkout> rev-parse HEAD)"`.

## Regenerating a vector

```bash
cd /Users/octopus/octo/code/blockchain-impl/op-geth
OPGETH_COMMIT=$(git rev-parse HEAD)
./opt8n --fork isthmus \
  --input  <repo>/bcos-evm/test/opstack/t8n/vectors/transfer_basic.in.json \
  --output <repo>/bcos-evm/test/opstack/t8n/vectors/isthmus_transfer_basic.json \
  --op-geth-commit "$OPGETH_COMMIT"
```

`--input` is a "case" file: the same v2 vector shape as the output, minus
`postState` and `_op_expected` (which the generator computes and appends),
plus a generator-only `secretKey` field on any tx that needs signing
(stripped from the output; only `_op_raw`/`v`/`r`/`s` survive). Input case
files live alongside their generated vectors, named `<name>.in.json`
(e.g. `transfer_basic.in.json` → `isthmus_transfer_basic.json`), so the
whole matrix can be regenerated in one batch per the plan's rule 3
("生成器与向量同 commit 入库；重生成必须整批").

**Rule 2 is mechanical, not aspirational**: this generator never accepts an
`expected`/`postState` field as input — the type (`inputVector`) simply has
no such fields, so hand-editing them into a case file has no effect on the
next regeneration.

## `isthmus_transfer_basic`: what it exercises and why the numbers are what they are

The vector is: (1) an L1 attributes deposit (first tx of the block, as on a
real OP-Stack chain) whose calldata encodes small, hand-chosen — not
`bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin` — L1 gas
parameters, followed by (2) a plain EIP-1559 ETH transfer.

**Why not the fixture bin file**: `isthmus_l1_attributes.bin`'s bytes are an
arbitrary/incrementing test pattern (`0x11223344...`, `0x0102030405060708...`)
originally meant to exercise the *parsing* code path — decoded, its
`l1BaseFee` alone is `0x0123456789abcdef` ≈ 8.2×10^16 wei. Reusing it
verbatim here would produce an L1 fee on the order of 10^23 wei for a basic
transfer, defeating the point of a hand-verifiable first vector, and it is
reserved for its intended purpose: Task 3's matrix row "L1 attributes
deposit（首笔）4 条", which exercises deposit-only blocks (no following user
tx) where `deriveOPStackFields` exits early and the fee magnitude therefore
never matters. This vector instead authors calldata in the same 176-byte
Isthmus layout (see `extractL1GasParamsPostIsthmus` in
`core/types/rollup_cost.go`) with values chosen to be simultaneously (a)
small enough to hand-verify and (b) **byte-identical**, slot for slot, to
the `L1Block` predeploy storage this vector's `pre` seeds directly (no
compiled `L1Block` bytecode is deployed, so nothing else would write these
slots) — so the informational `_op_l1_fee` receipt field and the real
state-driven L1 cost agree, as they would on a real chain where the deposit
tx's own execution keeps calldata and storage in sync.

One more prestate quirk found by running the generator: `L1Block` must have
**nonzero nonce** in `pre` (set to `1` here). Its EIP-158 "empty account"
status (nonce 0, balance 0, no code) combined with the deposit tx's `CALL`
touching it caused `handleDestruction` to try to wipe its (non-empty)
storage on commit, which trips `noStorageWiping` post-Cancun:
`unexpected storage wiping, 4200...0015`. Giving it nonce 1 marks it
non-empty without needing to deploy placeholder bytecode.

### Fee-conservation self-check (rule: "人工 sanity check 一次，此后不再人审")

All numbers below are read directly from the generator's own output
(`isthmus_transfer_basic.json`) — nothing here was invented; this is an
accounting-identity check over the program's output, not an independent
re-derivation of expected values.

```
gasUsed(transfer)          = 0x5208    = 21000
maxFeePerGas                = 0x77359400 = 2_000_000_000
maxPriorityFeePerGas         = 0x3b9aca00 = 1_000_000_000
currentBaseFee               = 0x3b9aca00 = 1_000_000_000
effectiveGasPrice = min(maxFeePerGas, baseFee+tip) = 2_000_000_000
l2Fee = gasUsed * effectiveGasPrice                = 42_000_000_000_000

receipt[1]._op_l1_fee        = 0x5f5e1000 = 1_600_000_000
postState[0x...1b (OperatorFeeVault)].balance = 0xf42a9  = 1_000_105
postState[0x...19 (BaseFeeVault)].balance     = 0x1319718a5000 = 21_000_000_000_000
postState[0x...11 (coinbase/SequencerFeeVault)].balance = 0x1319718a5000 = 21_000_000_000_000
postState[0x...1a (L1FeeVault)].balance       = 0x5f5e1000 = 1_600_000_000
value                          = 0xde0b6b3a7640000 = 1_000_000_000_000_000_000

Identities checked:
  BaseFeeVault   == gasUsed * baseFee                       -> 21000 * 1e9  = 21_000_000_000_000  ✓
  coinbase       == gasUsed * (effectiveGasPrice - baseFee) -> 21000 * 1e9  = 21_000_000_000_000  ✓
  L1FeeVault     == receipt._op_l1_fee                      -> 1_600_000_000                       ✓
  OperatorFeeVault == gasUsed*operatorFeeScalar/1e6 + operatorFeeConstant
                   = 21000*5000/1e6 + 1_000_000 = 105 + 1_000_000 = 1_000_105               ✓
  receiver.balance == value                                                                  ✓
  sender.pre - sender.post == value + l2Fee + l1Fee + operatorFeeVaultDelta
    10_000_000_000_000_000_000 - 8_999_957_998_398_999_895
    == 1_000_000_000_000_000_000 + 42_000_000_000_000 + 1_600_000_000 + 1_000_105
    == 1_000_042_001_601_000_105                                                             ✓
```

Deposit tx: `gasUsed = 0x5b04 = 23300` (intrinsic gas for a 176-byte-calldata
call to a code-less account), `blockGasUsed = 0xad0c = 44300 = 23300 +
21000`, matching cumulative gas exactly. `DepositNonce = 0x0` (the
depositor's nonce *before* this tx), `postState` shows the depositor's nonce
bumped to `0x1` afterwards and balance unchanged (mint=nil, value=0) — both
consistent with Regolith deposit-nonce semantics.

## Known simplifications (Task 1 scope; revisit in Task 3/4 if needed)

- Exactly one block per vector file (`processVector` errors on `len(blocks)
  != 1`). Nothing in the vector matrix (waves 0–2, per the plan) appears to
  need multi-block sequences, but this is an explicit, checked assumption,
  not silent truncation.
- `postState` is computed as a diff against `pre` for a bounded candidate
  set: every tx's `from`/`to`, plus the four fixed OP-Stack addresses
  (coinbase, BaseFeeVault, L1FeeVault, OperatorFeeVault). An address is
  included in `postState` only if its balance, nonce, code, or any of its
  *own pre-listed* storage slots actually changed — this is "every account
  that changed", the standard differential-test meaning of "touched
  account", not literally every account the EVM happened to read. It is not
  a full state-trie diff (no discovery of unforeseen storage writes to
  accounts outside the candidate set) — fine for the vector matrix so far,
  since no vector introduces contracts we haven't explicitly modeled in
  `pre`.
- Candidate pre-snapshots are taken "first-seen-in-tx-order" (before the tx
  that first mentions that address as `from`/`to` runs). Correct as long as
  no single address plays more than one role across the txs in one vector
  (e.g., sender == coinbase). None of the Task 1 vectors do; flag this if
  Task 3/4 ever needs to.
- No `rejectedTxs`/gas-pool-rollback handling — a `core.ApplyTransactionWithEVM`
  error aborts generation for the whole vector rather than being recorded as
  a "rejected" transaction. Fine for now: every vector so far is expected to
  produce a receipt (possibly `status=0` from an EVM-level revert, which
  *is* handled — that's not a Go error) rather than be rejected at the
  "would never be included in a block" level. Task 3's boundary-case row
  ("intrinsic 不足", "blob tx 拒绝", etc.) will need this if those cases are
  meant to prove the tx cannot be included in a block at all, rather than
  exercise a specific EVM revert.
