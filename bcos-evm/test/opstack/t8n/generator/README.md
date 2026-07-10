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

## Known limitations found during Task 3 (wave-1 vector authoring)

Three matrix cells could not be produced as executable vectors at all with this generator's
design. None of these are `bcos-evm/opstack` bugs or generator bugs to fix — they are structural
scope boundaries of "op-geth as a library, L1Block values pre-seeded rather than deployed as real
bytecode, fixed `--fork isthmus|jovian` chain configs". Recorded here (and cross-referenced from
`../vectors/DIVERGENCES.md`'s "Non-divergence known limitations" section) so Task 4/5 don't
rediscover them:

1. **`is_system_tx=true` once Regolith is active aborts the whole vector, not just the tx.**
   `stateTransition.preCheck()` returns `ErrSystemTxNotSupported` for any deposit with
   `is_system_tx=true` once Regolith is active (both `--fork isthmus` and `--fork jovian` have
   `RegolithTime=0`). `execute()`'s deposit-failure-absorbing branch explicitly excludes this
   error (`!errors.Is(err, ErrSystemTxNotSupported)`), so it propagates as a genuine Go error out
   of `core.ApplyTransactionWithEVM`, and `processVector` aborts (returns an error, no output file
   is written) rather than producing a vector with a "rejected" receipt. Verified by running it:
   ```
   $ ./opt8n --fork isthmus --input .../deposit_system_tx_rejected.in.json --output ... 
   opt8n: vector "isthmus_deposit_system_tx_rejected": tx 0: ApplyTransactionWithEVM: system tx not supported: address 0xdeadDEADdeAddeadDeadDeadDeaDDEADdead0011
   ```
   The attempted input case is kept as `../vectors/deposit_system_tx_rejected.in.json` for
   documentation purposes only (its `_info.comment` says so); it has no corresponding output
   `.json` and the replayer never sees it (only `*.in.json` are skipped by the replayer's file
   scan, and there is no matching output file to skip in the first place).
2. **No per-vector Canyon-time knob**, so a pre-Canyon (`DepositReceiptVersion=nil`) deposit
   receipt vector cannot be produced — `buildChainConfig` only exposes the two fixed,
   fully-post-Canyon presets (`--fork isthmus|jovian`). Out of scope per the plan's own escape
   hatch for this case ("若生成器暂不支持按向量调 fork time，就只做 post-Canyon 并在 README 记录限制").
3. **L1Block calldata-parsing ground truth is not achievable.** This generator deliberately never
   deploys `L1Block.sol` bytecode (see "L1Block predeploy" above); its own execution loop is real
   op-geth, and a `CALL` to a code-less account is a pure no-op in real EVM semantics. So real
   op-geth's ground truth for what an L1-attributes deposit's *own* calldata does to `L1Block`'s
   storage is always "no change", for any calldata content whatsoever — `bcos-evm/opstack`, in
   contrast, dispatches L1Block calls natively (`bcos-evm/opstack/l1/L1BlockStorage.cpp` always
   parses calldata and writes slots, independent of "code" being present at that address). A
   vector built to test this parsing path would therefore either have `L1Block` silently excluded
   from `postState` entirely (this generator's diff only emits accounts with an actual change
   relative to `pre`, and there never is one here), or, if `pre` were deliberately pre-seeded to
   match what the calldata implies, produce a vacuously "passing" comparison that doesn't actually
   exercise the parsing logic (bcos-evm reproducing values that were hand-placed into `pre` proves
   nothing about whether its parser is correct). This sank the originally planned wave-0 "l1_info
   attributes 解析场景" seed vector as an *executable* differential test; testing
   `L1BlockStorage.cpp`'s parsing correctness would need a different harness entirely (a
   hand-computed oracle, or teaching this generator to deploy compiled `L1Block.sol` bytecode into
   `pre`) — out of scope for this task.
4. **L1Block's `onlyDepositor` access control has no ground-truth counterpart either, for the same
   reason as #3** — but unlike #3 this one is a trap, not just a coverage gap: because op-geth
   never executes real `L1Block.sol` bytecode, its ground truth assumes *any* sender can call
   `setL1BlockValues*` successfully. `bcos-evm/opstack`'s native dispatch correctly enforces
   `msg.sender == OP_DEPOSITOR_ACCOUNT` (`0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001`,
   `L1BlockPredeploy.cpp:56-65`, `NotDepositor()` revert otherwise) — mirroring the real
   `L1Block.sol` contract a live chain deploys. Any vector whose L1-attributes deposit's `from` is
   *not* that exact address will replay as a spurious "divergence" (the deposit reverts in
   `bcos-evm/opstack`, succeeds in the generator's ground truth) that is really just a vector
   authoring mistake, not a finding — caught once, while authoring `isthmus_transfer_multi_nonce`
   (see `../vectors/DIVERGENCES.md`'s "Fixed pre-commit" section for the full story). **Every
   L1-attributes deposit vector's `_op_deposit.from` must be
   `0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001`.**

## Task 4 (wave 2): fee-env observers, EIP-7702, operator fee, boundaries

Task 4 added ~31 vectors across four categories (fee-environment observer contracts, EIP-7702 ×
OP-Stack, operator fee, and boundary/rejection cases) plus two generator enhancements
(`_op_type: "setcode"` and `_op_expect_rejected`). This section documents the design decisions;
`../vectors/DIVERGENCES.md` records the findings this wave produced.

### Fee-env observer contract (bytecode ↔ mnemonic table)

The observer contract used by every `*_observer_*` vector reads a fixed set of execution-time
environment values and `SSTORE`s each into a dedicated slot, so a single postState comparison
exposes every opcode's actual value — this is the "灵魂" of the wave: a hand-audited contract can
never catch a wrong `CHAINID`/`GASPRICE`/vault-balance-timing bug the way an actual op-geth-vs-
bcos-evm *execution* comparison can. Deployed at `0x00000000000000000000000000000000FEEDF00D`
(141 bytes). `pre` must declare all 14 slots with a placeholder value (`"0x00"`, doesn't matter
which — see "Why `pre` must pre-declare every observed slot" below) or the generator's diff never
looks at them and the vector's `postState` silently omits the contract entirely.

| Bytes (hex) | Mnemonic | Effect |
|---|---|---|
| `46 6000 55` | `CHAINID` `PUSH1 0x00` `SSTORE` | slot0 = chain ID |
| `3a 6001 55` | `GASPRICE` `PUSH1 0x01` `SSTORE` | slot1 = effective gas price |
| `41 6002 55` | `COINBASE` `PUSH1 0x02` `SSTORE` | slot2 = block coinbase |
| `48 6003 55` | `BASEFEE` `PUSH1 0x03` `SSTORE` | slot3 = block base fee |
| `32 6004 55` | `ORIGIN` `PUSH1 0x04` `SSTORE` | slot4 = tx origin |
| `43 6005 55` | `NUMBER` `PUSH1 0x05` `SSTORE` | slot5 = block number |
| `42 6006 55` | `TIMESTAMP` `PUSH1 0x06` `SSTORE` | slot6 = block timestamp |
| `5a 6007 55` | `GAS` `PUSH1 0x07` `SSTORE` | slot7 = gas remaining at this point |
| `73<vault11> 31 6008 55` | `PUSH20 <SequencerFeeVault/coinbase>` `BALANCE` `PUSH1 0x08` `SSTORE` | slot8 = vault 0x...0011 balance |
| `73<vault19> 31 6009 55` | `PUSH20 <BaseFeeVault>` `BALANCE` `PUSH1 0x09` `SSTORE` | slot9 = vault 0x...0019 balance |
| `73<vault1a> 31 600a 55` | `PUSH20 <L1FeeVault>` `BALANCE` `PUSH1 0x0a` `SSTORE` | slot10 = vault 0x...001a balance |
| `73<vault1b> 31 600b 55` | `PUSH20 <OperatorFeeVault>` `BALANCE` `PUSH1 0x0b` `SSTORE` | slot11 = vault 0x...001b balance |
| `47 600c 55` | `SELFBALANCE` `PUSH1 0x0c` `SSTORE` | slot12 = own balance (post value-transfer) |
| `34 600d 55` | `CALLVALUE` `PUSH1 0x0d` `SSTORE` | slot13 = msg.value |
| `00` | `STOP` | — |

Full literal: `0x466000553a60015541600255486003553260045543600555426006555a600755734200000000000000000000000000000000000011316008557342000000000000000000000000000000000000193160095573420000000000000000000000000000000000001a31600a5573420000000000000000000000000000000000001b31600b5547600c5534600d5500`
(assembled programmatically in the vector-authoring script, not hand-typed — see
`isthmus_observer_normal_basic.in.json`'s `pre` for the deployed copy).

**Design decisions embedded in the vectors, not the contract itself:**
- Every observer vector pre-funds the four OP-Stack vaults with small, distinct **marker**
  balances (e.g. 1001/1002/1003/1004) that are *not* what this same tx's own fee settlement would
  credit. The observed `BALANCE(vault)` reading back exactly the marker (not marker+credit) is the
  machine-checked confirmation that op-geth defers vault crediting until *after* EVM execution
  (`core.ApplyTransactionWithEVM` settlement runs after `stateTransitionExecute` returns) — spec
  §4.3's warning about not crediting vaults before execution, verified empirically rather than
  assumed. `isthmus_observer_deposit_vault_timing` is the cross-tx companion: it credits the vaults
  for real via an ordinary transfer in tx0, then reads them in tx1 to check *cross-tx* (not
  same-tx) visibility.
- `GAS` (slot7) is compared **exactly**, not just asserted non-zero as the plan's own escape hatch
  allowed ("如无法稳定则改为只 SSTORE 高位或干脆不测 GAS"). Between two spec-compliant EVMs executing
  identical fixed bytecode, gas remaining at a fixed point is deterministic — comparing it exactly
  is strictly more informative (a warm/cold access-list divergence, e.g. EIP-3651 warm-COINBASE,
  would show up here) and it did not prove flaky across any of the 12 observer vectors actually run.
- Deposit-context observer vectors (`*_observer_deposit_*`) exist specifically to empirically check
  `GASPRICE == 0` for a deposit tx's execution — the plan flagged this as resembling "spec §2's
  recorded Host defect", but reading `ApplyOpStackMessage.cpp` shows `ctx.gasPrice` is only ever set
  away from its zero default on the *normal*-tx path (`ctx.gasPrice = sidecar.effectiveGasPrice;`
  runs after the `view.isDeposit()` branch already returned) — i.e. the code looked correct by
  inspection. The vectors were built anyway (not skipped on the strength of that reading) per the
  plan's own instruction that this wave's job is to *empirically confirm or refute*, not assume; the
  replay confirmed op-geth and bcos-evm/opstack agree (`GASPRICE == 0` on both sides).

**Why `pre` must pre-declare every observed slot** (a load-bearing quirk of `diffPostState` in this
file — see the function's own comments): the diff only re-reads storage slots that were keys in
`pre[addr].storage`; a `SSTORE` to a slot that was never mentioned in `pre` never shows up in
`postState`, no matter what value it wrote. This was caught empirically: the first
`isthmus_observer_normal_basic` run produced a `postState` with the observer contract **entirely
absent** (balance/nonce/code all matched `pre`, and the diff never looked at any of the 14 slots
since none were pre-declared) despite the tx succeeding.

**Gas budget**: the observer contract's ~14 `SSTORE`s (most cold zero→nonzero, ~22100 gas each)
plus 4 cold `BALANCE` reads add up to roughly 300000-320000 gas — the first generation attempt used
`gasLimit=200000` (copied from the deposit/transfer vectors' convention) and every observer vector
reverted out of gas; bumped to `gasLimit=800000` (and the nested-call forwarder's own hardcoded
forwarded-gas amount to `1000000`, outer tx to `1200000`) fixes it with comfortable headroom.

### EIP-7702 vectors: field-form, not `_op_raw`-authoritative

Unlike every other tx kind in this vector matrix, `_op_type: "setcode"` (type 0x04) vectors are
**not** decoded from `_op_raw` by the replayer — they are read field-by-field (`_op_from`, `nonce`,
`to`, `value`, `gasLimit`, `maxFeePerGas`, `maxPriorityFeePerGas`, `data`, `_op_authorization_list`),
the same pattern `_op_deposit` already uses. `_op_raw` is still emitted by the generator (a real
`tx.MarshalBinary()` signed envelope) for documentation and as an optional DA-size source for
`rollupCostData`, but the replayer never RLP-decodes it for message construction or sender recovery.

**Why**: `bcos-rpc`'s `Web3Transaction` RLP codec (`bcos-rpc/web3jsonrpc/model/Web3Transaction.h`/
`.cpp`) only implements EIP-2718 types 0x00–0x03 (`TransactionType` enum tops out at `EIP4844 = 3`)
— there is no type-0x04/authorization-list decode path to reuse, and building one (plus replicating
its ecrecover-based `sender()` derivation for the type-0x04 signing-hash locally) is disproportionate
engineering for a 6-vector wave. It would also mean modifying `bcos-rpc`, a directory outside this
plan's authorized scope (spec rev.7 D3 authorizes changes to `bcos-evm` only).

**What this scope decision does *not* sacrifice**: the part of EIP-7702 this wave is actually
testing — authorization-tuple / delegation-installation semantics — is still exercised end-to-end
against a real op-geth signature. Each `_op_authorization_list` entry carries the genuine
`chainId`/`address`/`nonce`/`yParity`/`r`/`s` op-geth produced via `types.SignSetCode`; the replayer
feeds these unmodified into `bcos::evm::SetCodeAuthorization` (leaving `.authority` unset) so that
`bcos-evm/opstack`'s **own** `recoverAuthorizationAuthority` (`eth/eip/Eip7702.cpp`) performs the
real secp256k1 recovery at apply time — the same code path a live chain would exercise. Only the
*outer* tx's sender is taken on trust from the generator's `_op_from` (itself `crypto.PubkeyToAddress`
over the real signing key, not invented). See `T8nVectorReplayTest.cpp`'s `applySetCodeTx` doc
comment for the same writeup colocated with the code.

`generator/main.go`'s `"setcode"` case needed no chainConfig or signer changes at all:
`types.SetCodeTx` already exists upstream, and `types.MakeSigner` already returns an
Isthmus/Prague-capable signer for this generator's fixed chain config (`IsthmusTime=0` implies
Prague-equivalent rules are already active) — confirming the plan's own escape hatch ("先查 op-geth
的 types.SetCodeTx 与 t8ntool 的解析路径，若 _op_raw 已能覆盖则不必改生成器") in the negative direction:
the generator *did* need a new `buildTx` case (to actually construct+sign the tx and its
authorization tuples), but no signer/chainConfig plumbing.

**`DeriveFields` two-tx assumption caught by `isthmus_7702_extcodesize_extcodehash`**: this vector
is the first (across both Task 3 and Task 4) whose block has 2 transactions where the *first* is not
an L1-attributes deposit (it's a setcode tx). `types.Receipts.DeriveFields` unconditionally parses
`includedTxs[0].Data()` as 176-byte L1-attributes calldata whenever `len(txs) >= 2` — with a
non-deposit tx0 this fails (`"expected at least 260 L1 info bytes, got 0"`). Not an op-geth bug
(DeriveFields is documented as assuming a deposit-first block, the only shape upstream itself ever
produces) — fixed by gating the call on `includedTxs[0].Type() == types.DepositTxType`, which cannot
regress any existing deposit-first vector (the check is unconditionally true for all of them).

### `_op_expect_rejected`: vectors whose tx op-geth never includes

Some boundary cases are about a tx that a real block builder would never include at all
(insufficient funds, `gasLimit` below intrinsic/EIP-7623-floor gas) — Task 1's own "Known
simplifications" flagged this gap ("No `rejectedTxs`/gas-pool-rollback handling ... Task 3's
boundary-case row will need this"). Task 4 closes it: an input case tx may set
`"_op_expect_rejected": true`. The generator then:

1. Still attempts `TransactionToMessage`/`ApplyTransactionWithEVM` as normal.
2. On error, records `{txIndex, reason}` into the output vector's `_op_expected.rejected` (`reason`
   is op-geth's own Go error string, kept for human debugging only — never string-matched by the
   replayer) and **stops processing the rest of that block's transactions** (mirroring real
   block-building: a tx a builder would never include can't have "the next tx" be well-defined
   either).
3. If the tx instead succeeds, generation itself fails loudly (`"_op_expect_rejected=true but the
   tx was applied successfully"`) — a wrong boundary-case premise is a generation error, not a
   silently-accepted vector.
4. A `statedb.Snapshot()`/`RevertToSnapshot()` pair wraps the attempt: some rejections (EIP-7623
   floor gas, plain intrinsic gas) fire *after* `buyGas()` has already deducted fees from the
   sender's balance (`state_transition.go`'s `preCheck()` runs before the `IntrinsicGas`/
   `FloorDataGas` checks in `innerExecute()`) — without the snapshot/revert, a "rejected" tx's
   balance deduction would leak into `postState` despite the tx never actually being included.

On the replayer side, a vector whose `_op_expected.rejected.txIndex` matches the current tx index
skips `checkReceipt` (there is no receipt to compare against) and instead asserts `gasUsed == 0` —
this is deliberately reason-agnostic rather than trying to match op-geth's specific Go error string
(which `bcos-evm/opstack`'s own error messages never would anyway): any tx `applyOpStackMessage`'s
entry checks reject takes the `ctx.earlyExit` path and returns *before* settlement/execution, so
`gasUsed` stays at its zero default, while every genuinely-applied tx burns at least its intrinsic
gas (≥21000 for a plain transfer) — making `gasUsed == 0` a robust proxy for "op-geth and
bcos-evm/opstack agree this tx was never included," regardless of *why*.

**Two-pass precision derivation (`isthmus_boundary_l1cost_insufficient`,
`isthmus_boundary_value_zeroes_balance`)**: these vectors need the sender's balance to land on an
*exact* boundary (1 wei short of / exactly enough for `gasUsed × effectiveGasPrice + l1Fee`) that
depends on `l1Fee`, which is a Fjord fastlz-formula function of the raw signed tx's *bytes* — not
something hand-derivable without either replicating fastlz or just running the generator. The
authoring process was: (1) generate the same tx once with a generous placeholder balance, (2) read
the exact `pre.balance − postState.balance` delta off the real output (this delta already nets
gasUsed×price + l1Fee + operatorFee, no separate fastlz computation needed), (3) patch the case
file's balance to `delta − 1` (rejected) or `value + delta` (exactly zeroes out) and mark
`_op_expect_rejected` as appropriate, (4) regenerate for real. Both vectors' comments record the
exact numbers this produced (`42001600000000` wei for the shared 21000-gas/no-calldata/scalar=0/
constant=0 tx shape both vectors reuse).

**`isthmus_boundary_gaslimit_below_floor`/`isthmus_boundary_7623_floor_raises_gasused`**: EIP-7623's
`FloorDataGas` (`op-geth core/state_transition.go`) is `params.TxGas + tokens*10`
(**not** just `tokens*10`) — the `TxGas` (21000) base is included in the floor, not only in the
"normal" intrinsic-gas computation. Caught empirically: the first draft of these two vectors assumed
floor=40000 for 1000 bytes of all-nonzero calldata (`tokens=4000`, `4000*10=40000`); the real
generator output was `61000` (`21000 + 4000*10`), both in the rejection's `"want"` field and in the
succeeding vector's actual `gasUsed`. Comments in both `.in.json` files record the corrected formula
and the empirical value, not the original (wrong) hand-derivation.

**Not built as an executable vector: blob tx (type 0x03) rejection.** The plan's boundary row
included "blob tx（type 0x03）应被拒". Investigated and deliberately not attempted: this generator's
fixed chain configs (`--fork isthmus|jovian`) are both post-Cancun, and vanilla L1 Ethereum *does*
support blob transactions once Cancun is active — op-geth-as-a-library would process a well-formed
blob tx **normally**, not reject it. The rejection this boundary case wants to prove is a pure
OP-Stack-specific *policy* decision (no L1 beacon-chain blob availability at L2), which has no basis
in plain op-geth L1 execution semantics at all — there is no "op-geth ground truth" to differ
against in the sense this whole gate is built around. This is already covered by dedicated unit
tests (`bcos-evm/test/opstack/OpStackPreCheck4844Test.cpp`'s `rejects_type03_with_empty_hashes` et
al.), which is the right home for a pure-policy assertion; forcing it into this generator-truth
format would either need building full blob-tx (`types.BlobTx`) support into the generator for a
result the differential gate can't actually validate against (op-geth would say "succeeds", making
bcos-evm's correct rejection look like a divergence needing an attribution-(c) signoff), or produce
a vacuous vector. Recorded here — and in `../vectors/DIVERGENCES.md`'s "Non-divergence known
limitations" — instead.
