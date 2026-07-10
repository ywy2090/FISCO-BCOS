// Command opt8n is a minimal OP-Stack aware t8n ("transition tool") generator.
//
// It is NOT part of the FISCO-BCOS build. It is a standalone Go program whose
// execution loop is copied from upstream op-geth's
// cmd/evm/internal/t8ntool/execution.go ((*Prestate).Apply), with two
// deliberate substitutions documented in README.md:
//
//  1. chainConfig is assembled by hand from params.OptimismTestConfig (no
//     `tests.GetChainConfig` helper exists for OP-Stack forks upstream).
//  2. Transactions are parsed from a small JSON case format that supports
//     both raw signed envelopes (`_op_raw` / `secretKey`+fields) and OP
//     deposit transactions (`_op_type: "deposit"`).
//
// It reads an "input case" (a vector JSON with `env`/`pre`/`blocks` but no
// `postState` / `_op_expected`), executes it against a real op-geth EVM +
// StateDB, and writes back a fully populated "vector" JSON (v2 format, see
// bcos-evm/docs/superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md).
//
// This file is checked into the FISCO-BCOS repo as source-of-truth, but it
// is built from *inside* an op-geth checkout (see README.md) because it
// needs op-geth's internal packages (core, core/types, core/vm, params).
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"math/big"
	"os"
	"sort"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/common/math"
	"github.com/ethereum/go-ethereum/core"
	"github.com/ethereum/go-ethereum/core/rawdb"
	"github.com/ethereum/go-ethereum/core/state"
	"github.com/ethereum/go-ethereum/core/tracing"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/core/vm"
	"github.com/ethereum/go-ethereum/crypto"
	"github.com/ethereum/go-ethereum/ethdb"
	"github.com/ethereum/go-ethereum/params"
	"github.com/ethereum/go-ethereum/triedb"
	"github.com/holiman/uint256"
)

// generatorSchemaVersion is the `_op_test_vectors.version` written to every
// output file. Bump it whenever the v2 schema gains a new field.
const generatorSchemaVersion = "1.1.0"

func main() {
	var (
		fork         = flag.String("fork", "isthmus", "OP-Stack fork to activate: isthmus|jovian")
		inputPath    = flag.String("input", "", "input case JSON (env/pre/blocks, no expected/postState)")
		outputPath   = flag.String("output", "", "output vector JSON (input + postState + _op_expected)")
		opGethCommit = flag.String("op-geth-commit", "unknown", "op-geth checkout HEAD, recorded into _op_test_vectors.generator_commit")
	)
	flag.Parse()

	if *inputPath == "" || *outputPath == "" {
		fmt.Fprintln(os.Stderr, "usage: opt8n --fork isthmus|jovian --input <case.json> --output <vector.json> [--op-geth-commit <hash>]")
		os.Exit(2)
	}

	if err := run(*fork, *inputPath, *outputPath, *opGethCommit); err != nil {
		fmt.Fprintf(os.Stderr, "opt8n: %v\n", err)
		os.Exit(1)
	}
}

func run(fork, inputPath, outputPath, opGethCommit string) error {
	chainConfig, err := buildChainConfig(fork)
	if err != nil {
		return err
	}

	raw, err := os.ReadFile(inputPath)
	if err != nil {
		return fmt.Errorf("reading %s: %w", inputPath, err)
	}
	var file map[string]json.RawMessage
	if err := json.Unmarshal(raw, &file); err != nil {
		return fmt.Errorf("parsing %s: %w", inputPath, err)
	}

	out := make(map[string]json.RawMessage, len(file))
	ids := make([]string, 0, len(file))
	for id := range file {
		if id == "_op_test_vectors" {
			continue
		}
		ids = append(ids, id)
	}
	sort.Strings(ids)

	for _, id := range ids {
		vecOut, err := processVector(chainConfig, id, file[id])
		if err != nil {
			return fmt.Errorf("vector %q: %w", id, err)
		}
		out[id] = vecOut
	}

	meta, err := json.Marshal(struct {
		Version         string `json:"version"`
		Generator       string `json:"generator"`
		GeneratorCommit string `json:"generator_commit"`
		Fork            string `json:"fork"`
	}{generatorSchemaVersion, "opt8n", opGethCommit, fork})
	if err != nil {
		return err
	}
	out["_op_test_vectors"] = meta

	outBytes, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	outBytes = append(outBytes, '\n')
	if err := os.WriteFile(outputPath, outBytes, 0o644); err != nil {
		return fmt.Errorf("writing %s: %w", outputPath, err)
	}
	return nil
}

// buildChainConfig assembles the OP-Stack chain config for the requested
// fork. There is no `tests.GetChainConfig("isthmus")` upstream (that helper
// only knows plain-Ethereum fork names), so this is hand-assembled from
// params.OptimismTestConfig, confirmed against params/config.go and
// params/config_op.go (see README.md "chainConfig 装配" section for the
// grep transcript that pins these field names):
//
//	RegolithTime, CanyonTime, EcotoneTime, FjordTime, GraniteTime,
//	HoloceneTime, IsthmusTime, JovianTime, KarstTime *uint64
//	Optimism *OptimismConfig{ EIP1559Elasticity, EIP1559Denominator,
//	                          EIP1559DenominatorCanyon *uint64 }
func buildChainConfig(fork string) (*params.ChainConfig, error) {
	// params.OptimismTestConfig is a *params.ChainConfig package var; copying
	// the pointed-to struct by value gives us an independent ChainConfig we
	// can safely re-point (not mutate-through) fork-time fields on, without
	// disturbing the shared upstream instance.
	conf := *params.OptimismTestConfig
	// Use a distinct chain ID so vectors read unambiguously (8453 = Base
	// mainnet, matches the plan's v2-format example).
	conf.ChainID = big.NewInt(8453)

	switch fork {
	case "isthmus":
		// OptimismTestConfig already has Regolith..Isthmus = time 0; Jovian
		// must be disabled for a pure-Isthmus vector.
		conf.JovianTime = nil
	case "jovian":
		// OptimismTestConfig already sets JovianTime = 0.
	default:
		return nil, fmt.Errorf("unknown --fork %q (want isthmus|jovian)", fork)
	}
	return &conf, nil
}

// ---------------------------------------------------------------------
// Input schema (case JSON: env/pre/blocks, no expected/postState)
// ---------------------------------------------------------------------

type inputVector struct {
	Info   json.RawMessage    `json:"_info"`
	Env    inputEnv           `json:"env"`
	Pre    types.GenesisAlloc `json:"pre"`
	Blocks []inputBlock       `json:"blocks"`
}

type inputEnv struct {
	CurrentCoinbase       common.Address        `json:"currentCoinbase"`
	CurrentNumber         math.HexOrDecimal64   `json:"currentNumber"`
	CurrentTimestamp      math.HexOrDecimal64   `json:"currentTimestamp"`
	CurrentGasLimit       math.HexOrDecimal64   `json:"currentGasLimit"`
	CurrentBaseFee        *math.HexOrDecimal256 `json:"currentBaseFee,omitempty"`
	CurrentRandom         *math.HexOrDecimal256 `json:"currentRandom,omitempty"`
	ParentBeaconBlockRoot *common.Hash          `json:"parentBeaconBlockRoot,omitempty"`
}

type inputBlockHeader struct {
	Number    math.HexOrDecimal64 `json:"number"`
	Timestamp math.HexOrDecimal64 `json:"timestamp"`
	GasLimit  math.HexOrDecimal64 `json:"gasLimit"`
}

type inputBlock struct {
	BlockHeader  inputBlockHeader `json:"blockHeader"`
	Transactions []inputTx        `json:"transactions"`
}

// inputDeposit mirrors op-geth's types.DepositTx, minus SourceHash's
// convenience defaulting.
type inputDeposit struct {
	From       common.Address        `json:"from"`
	To         *common.Address       `json:"to"`
	Mint       *math.HexOrDecimal256 `json:"mint,omitempty"`
	Value      *math.HexOrDecimal256 `json:"value,omitempty"`
	IsSystemTx bool                  `json:"is_system_tx"`
	SourceHash common.Hash           `json:"source_hash"`
}

// inputAuthorization is one EIP-7702 authorization-tuple case entry: the
// generator signs it (types.SignSetCode) with AuthSecretKey, independently of
// the outer tx's own SecretKey (the two keys are usually different -- the
// whole point of EIP-7702 is that a *different* EOA delegates code to itself
// while a *sponsor* EOA pays for and sends the tx).
type inputAuthorization struct {
	ChainID       *math.HexOrDecimal256 `json:"chainId,omitempty"`
	Address       common.Address        `json:"address"`
	Nonce         math.HexOrDecimal64   `json:"nonce"`
	AuthSecretKey hexutil.Bytes         `json:"authSecretKey"`
}

// inputTx is a tagged union: _op_type == "deposit" uses OpDeposit,
// "setcode" uses OpAuthorizations (EIP-7702, type 0x04); anything else
// (including the empty string, defaulting to "eip1559") is a normal
// signed transaction, either given pre-signed via OpRaw, or given field-by-
// field plus a SecretKey for the generator to sign.
type inputTx struct {
	OpType           string               `json:"_op_type,omitempty"`
	OpDeposit        *inputDeposit        `json:"_op_deposit,omitempty"`
	OpAuthorizations []inputAuthorization `json:"_op_authorization_list,omitempty"`
	OpRaw            string               `json:"_op_raw,omitempty"`

	Nonce                *math.HexOrDecimal64  `json:"nonce,omitempty"`
	To                   *common.Address       `json:"to,omitempty"`
	Value                *math.HexOrDecimal256 `json:"value,omitempty"`
	GasLimit             *math.HexOrDecimal64  `json:"gasLimit,omitempty"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas,omitempty"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas,omitempty"`
	Data                 hexutil.Bytes         `json:"data,omitempty"`
	ChainID              *math.HexOrDecimal256 `json:"chainId,omitempty"`
	AccessList           types.AccessList      `json:"accessList,omitempty"`
	SecretKey            *hexutil.Bytes        `json:"secretKey,omitempty"`

	// OpExpectRejected marks a tx that this case *intends* to be rejected before
	// or during application (insufficient funds, gasLimit below intrinsic/EIP-7623
	// floor gas, etc.) -- i.e. a tx that a real block builder would never include.
	// Normally any error from TransactionToMessage/ApplyTransactionWithEVM aborts
	// the whole vector's generation (`Known limitations` #1 in README.md: a Go
	// error is otherwise indistinguishable from a vector-authoring mistake). This
	// flag lets Task 4's boundary-case row ("intrinsic 不足", "gasLimit < floor
	// 的拒绝", "余额差 1 wei 付不起 l1Cost") turn that into an *expected*, checked
	// outcome instead: the generator records the failure into
	// `_op_expected.rejected` and stops processing that block's remaining
	// transactions (matching real block-building semantics -- a tx a builder
	// would never include can't have "the next tx after it" be well-defined
	// either). If the tx instead *succeeds*, that is now itself a generation
	// error (the case's premise was wrong), not a silently-accepted vector.
	OpExpectRejected bool `json:"_op_expect_rejected,omitempty"`
}

// ---------------------------------------------------------------------
// Output schema (v2 vector: input + postState + _op_expected)
// ---------------------------------------------------------------------

type outputDepositTx struct {
	OpType    string              `json:"_op_type"`
	OpDeposit inputDeposit        `json:"_op_deposit"`
	GasLimit  math.HexOrDecimal64 `json:"gasLimit"`
	Data      hexutil.Bytes       `json:"data"`
}

// Note: math.HexOrDecimal256's MarshalJSON/MarshalText are defined on a
// *pointer* receiver, and its underlying big.Int has no exported fields. A
// struct field of the bare (non-pointer) type therefore silently serializes
// as "{}" instead of a hex string (json.Marshal is given a non-addressable
// copy of the struct, so it cannot promote to the pointer method set). Every
// HexOrDecimal256 field below must stay a pointer for this reason.
type outputSignedTx struct {
	OpType               string                `json:"_op_type"`
	Type                 string                `json:"type"`
	Nonce                math.HexOrDecimal64   `json:"nonce"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas"`
	GasLimit             math.HexOrDecimal64   `json:"gasLimit"`
	To                   *common.Address       `json:"to"`
	Value                *math.HexOrDecimal256 `json:"value"`
	Data                 hexutil.Bytes         `json:"data"`
	ChainID              *math.HexOrDecimal256 `json:"chainId"`
	AccessList           types.AccessList      `json:"accessList"`
	V                    *math.HexOrDecimal256 `json:"v"`
	R                    *math.HexOrDecimal256 `json:"r"`
	S                    *math.HexOrDecimal256 `json:"s"`
	OpRaw                string                `json:"_op_raw"`
}

// outputAuthorization is the field-form -- not RLP -- record of one signed
// EIP-7702 authorization tuple. `Authority` is included purely as a
// documentation/self-check convenience (computed via op-geth's own
// SetCodeAuthorization.Authority(), i.e. real secp256k1 recovery over the
// real signed payload); the replayer does NOT trust it -- it feeds
// ChainID/Address/Nonce/YParity/R/S into bcos-evm/opstack's own
// recoverAuthorizationAuthority (eth/eip/Eip7702.cpp) and lets *that*
// production code do its own recovery, so a wrong `Authority` value here
// would never mask an authority-recovery bug (see generator/README.md's
// "EIP-7702 vectors: field-form, not _op_raw-authoritative" section for why
// this tx kind is field-form while eip1559/deposit are _op_raw-driven).
type outputAuthorization struct {
	ChainID   *math.HexOrDecimal256 `json:"chainId"`
	Address   common.Address        `json:"address"`
	Nonce     math.HexOrDecimal64   `json:"nonce"`
	Authority common.Address        `json:"authority"`
	YParity   math.HexOrDecimal64   `json:"yParity"`
	R         *math.HexOrDecimal256 `json:"r"`
	S         *math.HexOrDecimal256 `json:"s"`
}

// outputSetCodeTx mirrors outputSignedTx but for EIP-7702 (type 0x04).
// `_op_raw` is still emitted (real op-geth signed envelope, `tx.MarshalBinary()`)
// for documentation/future-proofing and DA-size sourcing (rollupCostData), but
// -- unlike eip1559 -- the replayer does not RLP-decode it to build the
// message or recover the sender; see the OpAuthorizations/outputAuthorization
// doc comments for why, and `_op_from` below for the substitute.
type outputSetCodeTx struct {
	OpType               string                `json:"_op_type"`
	Type                 string                `json:"type"`
	OpFrom               common.Address        `json:"_op_from"`
	Nonce                math.HexOrDecimal64   `json:"nonce"`
	MaxFeePerGas         *math.HexOrDecimal256 `json:"maxFeePerGas"`
	MaxPriorityFeePerGas *math.HexOrDecimal256 `json:"maxPriorityFeePerGas"`
	GasLimit             math.HexOrDecimal64   `json:"gasLimit"`
	To                   common.Address        `json:"to"`
	Value                *math.HexOrDecimal256 `json:"value"`
	Data                 hexutil.Bytes         `json:"data"`
	ChainID              *math.HexOrDecimal256 `json:"chainId"`
	AccessList           types.AccessList      `json:"accessList"`
	OpAuthorizationList  []outputAuthorization `json:"_op_authorization_list"`
	V                    *math.HexOrDecimal256 `json:"v"`
	R                    *math.HexOrDecimal256 `json:"r"`
	S                    *math.HexOrDecimal256 `json:"s"`
	OpRaw                string                `json:"_op_raw"`
}

type outputBlock struct {
	BlockHeader  inputBlockHeader  `json:"blockHeader"`
	Transactions []json.RawMessage `json:"transactions"`
}

type expectedReceipt struct {
	Type                    string  `json:"type"`
	Status                  string  `json:"status"`
	GasUsed                 string  `json:"gasUsed"`
	LogsCount               int     `json:"logsCount"`
	OpDepositNonce          *string `json:"_op_deposit_nonce,omitempty"`
	OpDepositReceiptVersion *string `json:"_op_deposit_receipt_version,omitempty"`
	OpL1Fee                 *string `json:"_op_l1_fee,omitempty"`
}

// expectedRejection records that block inclusion stopped at TxIndex because
// that tx would never be included in a real block (see inputTx.OpExpectRejected).
// Reason is op-geth's own error string, kept for human debugging only -- the
// replayer does not string-match it (op-geth's and bcos-evm/opstack's error
// messages are naturally different); it only checks that bcos-evm/opstack
// *also* refuses to apply the tx (empty state diff, zero gasUsed -- see
// T8nVectorReplayTest.cpp's applyExpectRejectedTx).
type expectedRejection struct {
	TxIndex int    `json:"txIndex"`
	Reason  string `json:"reason"`
}

type opExpected struct {
	Receipts     []expectedReceipt  `json:"receipts"`
	BlockGasUsed string             `json:"blockGasUsed"`
	Rejected     *expectedRejection `json:"rejected,omitempty"`
}

type outputVector struct {
	Info       json.RawMessage    `json:"_info"`
	Env        inputEnv           `json:"env"`
	Pre        types.GenesisAlloc `json:"pre"`
	Blocks     []outputBlock      `json:"blocks"`
	PostState  types.GenesisAlloc `json:"postState"`
	OpExpected opExpected         `json:"_op_expected"`
}

// ---------------------------------------------------------------------
// Execution: loop body copied from op-geth cmd/evm/internal/t8ntool
// execution.go's (*Prestate).Apply, trimmed to what OP-Stack vectors need
// (no blob txs, no EIP-6110/7002/7251 requests, no verkle, no mining
// reward/ommers/withdrawals) and extended with the OP-Stack L1Cost /
// OperatorCost block-context wiring that upstream's Apply() does NOT do
// (see core/evm.go NewEVMBlockContext for the pattern being mirrored).
// ---------------------------------------------------------------------

func processVector(chainConfig *params.ChainConfig, id string, raw json.RawMessage) (json.RawMessage, error) {
	var in inputVector
	if err := json.Unmarshal(raw, &in); err != nil {
		return nil, err
	}
	if len(in.Blocks) != 1 {
		return nil, fmt.Errorf("exactly one block is supported by this generator version, got %d", len(in.Blocks))
	}
	blk := in.Blocks[0]

	blockNumber := uint64(in.Env.CurrentNumber)
	if blk.BlockHeader.Number != 0 {
		blockNumber = uint64(blk.BlockHeader.Number)
	}
	blockTime := uint64(in.Env.CurrentTimestamp)
	if blk.BlockHeader.Timestamp != 0 {
		blockTime = uint64(blk.BlockHeader.Timestamp)
	}
	gasLimit := uint64(in.Env.CurrentGasLimit)
	if blk.BlockHeader.GasLimit != 0 {
		gasLimit = uint64(blk.BlockHeader.GasLimit)
	}

	statedb := MakePreState(rawdb.NewMemoryDatabase(), in.Pre)
	signer := types.MakeSigner(chainConfig, new(big.Int).SetUint64(blockNumber), blockTime)
	gaspool := core.NewGasPool(gasLimit)
	blockHash := common.Hash{0x13, 0x37} // t8ntool placeholder convention; no real chain to derive it from.

	var baseFee *big.Int
	if in.Env.CurrentBaseFee != nil {
		baseFee = (*big.Int)(in.Env.CurrentBaseFee)
	}

	vmContext := vm.BlockContext{
		CanTransfer: core.CanTransfer,
		Transfer:    core.Transfer,
		Coinbase:    in.Env.CurrentCoinbase,
		BlockNumber: new(big.Int).SetUint64(blockNumber),
		Time:        blockTime,
		Difficulty:  big.NewInt(0),
		GasLimit:    gasLimit,
		BaseFee:     baseFee,
		GetHash: func(n uint64) common.Hash {
			return common.Hash{} // no BLOCKHASH support needed by this vector matrix (yet)
		},
	}
	if in.Env.CurrentRandom != nil {
		rnd := common.BigToHash((*big.Int)(in.Env.CurrentRandom))
		vmContext.Random = &rnd
	}

	// --- Substitution #2 (see README.md): OP-Stack L1Cost / OperatorCost
	// wiring. Vanilla t8ntool's Apply() never sets these BlockContext
	// fields, because vanilla ethereum/tests vectors have no L1Block
	// predeploy. We mirror core/evm.go's NewEVMBlockContext exactly: both
	// funcs are lazy closures over the *mutable* statedb, so they pick up
	// whatever the first (L1 attributes deposit) transaction of the block
	// writes to the L1Block predeploy's storage slots -- for this generator
	// the values are pre-seeded directly into `pre` instead (no compiled
	// L1Block bytecode is deployed), which is consensus-equivalent as long
	// as the deposit tx's own calldata is never relied upon by these two
	// functions (it isn't; they read state, not calldata).
	vmContext.L1CostFunc = types.NewL1CostFunc(chainConfig, statedb)
	if chainConfig.IsOptimismIsthmus(blockTime) {
		vmContext.OperatorCostFunc = types.NewOperatorCostFunc(chainConfig, statedb)
	}

	evm := vm.NewEVM(vmContext, statedb, chainConfig, vm.Config{})
	if in.Env.ParentBeaconBlockRoot != nil {
		core.ProcessBeaconBlockRoot(*in.Env.ParentBeaconBlockRoot, evm)
	}

	var (
		receipts    types.Receipts
		includedTxs types.Transactions
		outTxs      []json.RawMessage
		candidates  = map[common.Address]struct{}{
			in.Env.CurrentCoinbase:              {},
			params.OptimismBaseFeeRecipient:     {},
			params.OptimismL1FeeRecipient:       {},
			params.OptimismOperatorFeeRecipient: {},
		}
	)
	preSnapshot := snapshotAccounts(in.Pre, candidates) // extended below, per-tx, before it fades from scope

	var rejected *expectedRejection
	for i := range blk.Transactions {
		txIn := &blk.Transactions[i]
		tx, outTx, err := buildTx(txIn, signer)
		if err != nil {
			return nil, fmt.Errorf("tx %d: %w", i, err)
		}
		outTxs = append(outTxs, outTx)

		msg, err := core.TransactionToMessage(tx, signer, baseFee)
		if err != nil {
			if txIn.OpExpectRejected {
				rejected = &expectedRejection{TxIndex: i, Reason: err.Error()}
				break
			}
			return nil, fmt.Errorf("tx %d: TransactionToMessage: %w", i, err)
		}
		candidates[msg.From] = struct{}{}
		if msg.To != nil {
			candidates[*msg.To] = struct{}{}
		}
		// Snapshot any newly-discovered candidate's pre-state now, before
		// this tx mutates it.
		addMissingSnapshots(preSnapshot, in.Pre, candidates)

		// Snapshot statedb too: some rejections (EIP-7623 floor gas, intrinsic
		// gas) fire *after* buyGas() has already deducted the fee from the
		// sender's balance (state_transition.go: preCheck()/buyGas() runs
		// before the IntrinsicGas/FloorDataGas checks in innerExecute()) --
		// unlike a real block builder (miner/worker.go's commitTransaction),
		// this loop has no surrounding snapshot/revert of its own, so without
		// this the rejected tx's balance deduction would leak into postState
		// despite the tx never actually being included.
		stateSnapshot := statedb.Snapshot()
		statedb.SetTxContext(tx.Hash(), i)
		receipt, err := core.ApplyTransactionWithEVM(msg, gaspool, statedb, vmContext.BlockNumber, blockHash, blockTime, tx, evm)
		if err != nil {
			if txIn.OpExpectRejected {
				statedb.RevertToSnapshot(stateSnapshot)
				rejected = &expectedRejection{TxIndex: i, Reason: err.Error()}
				break
			}
			return nil, fmt.Errorf("tx %d: ApplyTransactionWithEVM: %w", i, err)
		}
		if txIn.OpExpectRejected {
			return nil, fmt.Errorf("tx %d: _op_expect_rejected=true but the tx was applied successfully (status=%d) -- the case's premise is wrong", i, receipt.Status)
		}
		if receipt.Logs == nil {
			receipt.Logs = []*types.Log{}
		}
		includedTxs = append(includedTxs, tx)
		receipts = append(receipts, receipt)
	}

	statedb.IntermediateRoot(chainConfig.IsEIP158(vmContext.BlockNumber))
	root, err := statedb.Commit(blockNumber, chainConfig.IsEIP158(vmContext.BlockNumber), chainConfig.IsCancun(vmContext.BlockNumber, blockTime))
	if err != nil {
		return nil, fmt.Errorf("commit: %w", err)
	}
	statedb, err = state.New(root, statedb.Database())
	if err != nil {
		return nil, fmt.Errorf("reopen state: %w", err)
	}

	// Populate OP-Stack receipt display fields (L1Fee/L1GasPrice/scalars).
	// Requires >= 2 txs *and* the first one to be a real L1-attributes deposit
	// (types.Receipts.deriveOPStackFields parses includedTxs[0].Data() as the
	// 176-byte Isthmus L1-attributes calldata layout unconditionally whenever
	// len(txs) >= 2 -- its doc comment's ">= 2 txs" description assumes a
	// deposit-first block, which is the only shape upstream ever produces;
	// Task 4 introduced vectors whose first tx is a plain eip1559/setcode tx
	// with a *second* tx following it (e.g. isthmus_7702_extcodesize_extcodehash's
	// setcode-then-eip1559 pair), which trips this: "expected at least 260 L1
	// info bytes, got 0". Guarded here rather than upstream since this is this
	// generator's own trimmed-loop assumption, not an op-geth bug.
	if len(includedTxs) >= 2 && includedTxs[0].Type() == types.DepositTxType {
		if err := receipts.DeriveFields(chainConfig, blockHash, blockNumber, blockTime, baseFee, nil, includedTxs); err != nil {
			return nil, fmt.Errorf("DeriveFields: %w", err)
		}
	}

	postState := diffPostState(statedb, preSnapshot)

	expReceipts := make([]expectedReceipt, len(receipts))
	var blockGasUsed uint64
	for i, r := range receipts {
		blockGasUsed = r.CumulativeGasUsed
		er := expectedReceipt{
			Type:      hexutil.EncodeUint64(uint64(r.Type)),
			Status:    hexutil.EncodeUint64(r.Status),
			GasUsed:   hexutil.EncodeUint64(r.GasUsed),
			LogsCount: len(r.Logs),
		}
		if r.DepositNonce != nil {
			s := hexutil.EncodeUint64(*r.DepositNonce)
			er.OpDepositNonce = &s
		}
		if r.DepositReceiptVersion != nil {
			s := hexutil.EncodeUint64(*r.DepositReceiptVersion)
			er.OpDepositReceiptVersion = &s
		}
		if r.L1Fee != nil {
			s := hexutil.EncodeBig(r.L1Fee)
			er.OpL1Fee = &s
		}
		expReceipts[i] = er
	}

	out := outputVector{
		Info: in.Info,
		Env:  in.Env,
		Pre:  in.Pre,
		Blocks: []outputBlock{{
			BlockHeader:  blk.BlockHeader,
			Transactions: outTxs,
		}},
		PostState: postState,
		OpExpected: opExpected{
			Receipts:     expReceipts,
			BlockGasUsed: hexutil.EncodeUint64(blockGasUsed),
			Rejected:     rejected,
		},
	}
	return json.Marshal(out)
}

func buildTx(in *inputTx, signer types.Signer) (*types.Transaction, json.RawMessage, error) {
	switch in.OpType {
	case "deposit":
		if in.OpDeposit == nil {
			return nil, nil, fmt.Errorf(`_op_type="deposit" requires "_op_deposit"`)
		}
		d := in.OpDeposit
		var mint *big.Int
		if d.Mint != nil {
			mint = (*big.Int)(d.Mint)
		}
		value := big.NewInt(0)
		if d.Value != nil {
			value = (*big.Int)(d.Value)
		}
		var gas uint64
		if in.GasLimit != nil {
			gas = uint64(*in.GasLimit)
		}
		tx := types.NewTx(&types.DepositTx{
			SourceHash:          d.SourceHash,
			From:                d.From,
			To:                  d.To,
			Mint:                mint,
			Value:               value,
			Gas:                 gas,
			IsSystemTransaction: d.IsSystemTx,
			Data:                []byte(in.Data),
		})
		outJSON, err := json.Marshal(outputDepositTx{
			OpType:    "deposit",
			OpDeposit: *d,
			GasLimit:  math.HexOrDecimal64(gas),
			Data:      in.Data,
		})
		return tx, outJSON, err

	case "setcode":
		// EIP-7702 (type 0x04). Per README.md's "Substitution 2" and the plan's
		// own escape hatch ("先查 op-geth 的 types.SetCodeTx ... 若 _op_raw 已能覆盖则不必改生成器"):
		// types.SetCodeTx already exists upstream and MakeSigner already returns an
		// Isthmus/Prague-capable signer for our chainConfig (IsthmusTime=0 implies
		// PragueTime=0 is also active), so building+signing this tx type needs no
		// signer/chainConfig changes at all -- only a new buildTx case.
		if in.SecretKey == nil {
			return nil, nil, fmt.Errorf(`_op_type="setcode" requires "secretKey" (the sponsor/sender)`)
		}
		if in.To == nil {
			return nil, nil, fmt.Errorf(`_op_type="setcode" requires "to" (EIP-7702 txs cannot create contracts)`)
		}
		prv, err := crypto.ToECDSA(*in.SecretKey)
		if err != nil {
			return nil, nil, fmt.Errorf("parsing secretKey: %w", err)
		}
		fromAddr := crypto.PubkeyToAddress(prv.PublicKey)
		chainID := big.NewInt(0)
		if in.ChainID != nil {
			chainID = (*big.Int)(in.ChainID)
		}
		var nonce uint64
		if in.Nonce != nil {
			nonce = uint64(*in.Nonce)
		}
		var gas uint64
		if in.GasLimit != nil {
			gas = uint64(*in.GasLimit)
		}
		value := big.NewInt(0)
		if in.Value != nil {
			value = (*big.Int)(in.Value)
		}
		tip, feeCap := big.NewInt(0), big.NewInt(0)
		if in.MaxPriorityFeePerGas != nil {
			tip = (*big.Int)(in.MaxPriorityFeePerGas)
		}
		if in.MaxFeePerGas != nil {
			feeCap = (*big.Int)(in.MaxFeePerGas)
		}
		al := in.AccessList
		if al == nil {
			al = types.AccessList{}
		}
		if len(in.OpAuthorizations) == 0 {
			return nil, nil, fmt.Errorf(`_op_type="setcode" requires a non-empty "_op_authorization_list"`)
		}
		authList := make([]types.SetCodeAuthorization, len(in.OpAuthorizations))
		outAuths := make([]outputAuthorization, len(in.OpAuthorizations))
		for i, a := range in.OpAuthorizations {
			authPrv, err := crypto.ToECDSA(a.AuthSecretKey)
			if err != nil {
				return nil, nil, fmt.Errorf("_op_authorization_list[%d]: parsing authSecretKey: %w", i, err)
			}
			authChainID := big.NewInt(0)
			if a.ChainID != nil {
				authChainID = (*big.Int)(a.ChainID)
			}
			unsigned := types.SetCodeAuthorization{
				ChainID: *uint256.MustFromBig(authChainID),
				Address: a.Address,
				Nonce:   uint64(a.Nonce),
			}
			signed, err := types.SignSetCode(authPrv, unsigned)
			if err != nil {
				return nil, nil, fmt.Errorf("_op_authorization_list[%d]: SignSetCode: %w", i, err)
			}
			authList[i] = signed
			authority, err := signed.Authority()
			if err != nil {
				return nil, nil, fmt.Errorf("_op_authorization_list[%d]: Authority(): %w", i, err)
			}
			outAuths[i] = outputAuthorization{
				ChainID:   (*math.HexOrDecimal256)(authChainID),
				Address:   a.Address,
				Nonce:     a.Nonce,
				Authority: authority,
				YParity:   math.HexOrDecimal64(signed.V),
				R:         (*math.HexOrDecimal256)(signed.R.ToBig()),
				S:         (*math.HexOrDecimal256)(signed.S.ToBig()),
			}
		}
		txdata := &types.SetCodeTx{
			ChainID:    uint256.MustFromBig(chainID),
			Nonce:      nonce,
			GasTipCap:  uint256.MustFromBig(tip),
			GasFeeCap:  uint256.MustFromBig(feeCap),
			Gas:        gas,
			To:         *in.To,
			Value:      uint256.MustFromBig(value),
			Data:       []byte(in.Data),
			AccessList: al,
			AuthList:   authList,
		}
		tx, err := types.SignNewTx(prv, signer, txdata)
		if err != nil {
			return nil, nil, fmt.Errorf("signing setcode tx: %w", err)
		}
		rawBin, err := tx.MarshalBinary()
		if err != nil {
			return nil, nil, err
		}
		v, r, s := tx.RawSignatureValues()
		outJSON, err := json.Marshal(outputSetCodeTx{
			OpType:               "setcode",
			Type:                 "0x04",
			OpFrom:               fromAddr,
			Nonce:                math.HexOrDecimal64(nonce),
			MaxFeePerGas:         (*math.HexOrDecimal256)(feeCap),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tip),
			GasLimit:             math.HexOrDecimal64(gas),
			To:                   *in.To,
			Value:                (*math.HexOrDecimal256)(value),
			Data:                 in.Data,
			ChainID:              (*math.HexOrDecimal256)(chainID),
			AccessList:           al,
			OpAuthorizationList:  outAuths,
			V:                    (*math.HexOrDecimal256)(v),
			R:                    (*math.HexOrDecimal256)(r),
			S:                    (*math.HexOrDecimal256)(s),
			OpRaw:                hexutil.Encode(rawBin),
		})
		return tx, outJSON, err

	case "", "eip1559":
		if in.OpRaw != "" {
			rawBytes, err := hexutil.Decode(in.OpRaw)
			if err != nil {
				return nil, nil, fmt.Errorf("decoding _op_raw: %w", err)
			}
			tx := new(types.Transaction)
			if err := tx.UnmarshalBinary(rawBytes); err != nil {
				return nil, nil, fmt.Errorf("unmarshalling _op_raw: %w", err)
			}
			// Already signed: pass the original JSON object straight through
			// (it must already carry v/r/s/_op_raw).
			passthrough, err := json.Marshal(in)
			return tx, passthrough, err
		}
		if in.SecretKey == nil {
			return nil, nil, fmt.Errorf("normal tx requires either _op_raw or secretKey")
		}
		prv, err := crypto.ToECDSA(*in.SecretKey)
		if err != nil {
			return nil, nil, fmt.Errorf("parsing secretKey: %w", err)
		}
		chainID := big.NewInt(0)
		if in.ChainID != nil {
			chainID = (*big.Int)(in.ChainID)
		}
		var nonce uint64
		if in.Nonce != nil {
			nonce = uint64(*in.Nonce)
		}
		var gas uint64
		if in.GasLimit != nil {
			gas = uint64(*in.GasLimit)
		}
		value := big.NewInt(0)
		if in.Value != nil {
			value = (*big.Int)(in.Value)
		}
		tip, feeCap := big.NewInt(0), big.NewInt(0)
		if in.MaxPriorityFeePerGas != nil {
			tip = (*big.Int)(in.MaxPriorityFeePerGas)
		}
		if in.MaxFeePerGas != nil {
			feeCap = (*big.Int)(in.MaxFeePerGas)
		}
		al := in.AccessList
		if al == nil {
			al = types.AccessList{}
		}
		txdata := &types.DynamicFeeTx{
			ChainID:    chainID,
			Nonce:      nonce,
			GasTipCap:  tip,
			GasFeeCap:  feeCap,
			Gas:        gas,
			To:         in.To,
			Value:      value,
			Data:       []byte(in.Data),
			AccessList: al,
		}
		tx, err := types.SignNewTx(prv, signer, txdata)
		if err != nil {
			return nil, nil, fmt.Errorf("signing tx: %w", err)
		}
		rawBin, err := tx.MarshalBinary()
		if err != nil {
			return nil, nil, err
		}
		v, r, s := tx.RawSignatureValues()
		outJSON, err := json.Marshal(outputSignedTx{
			OpType:               "eip1559",
			Type:                 "0x02",
			Nonce:                math.HexOrDecimal64(nonce),
			MaxFeePerGas:         (*math.HexOrDecimal256)(feeCap),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tip),
			GasLimit:             math.HexOrDecimal64(gas),
			To:                   in.To,
			Value:                (*math.HexOrDecimal256)(value),
			Data:                 in.Data,
			ChainID:              (*math.HexOrDecimal256)(chainID),
			AccessList:           al,
			V:                    (*math.HexOrDecimal256)(v),
			R:                    (*math.HexOrDecimal256)(r),
			S:                    (*math.HexOrDecimal256)(s),
			OpRaw:                hexutil.Encode(rawBin),
		})
		return tx, outJSON, err

	default:
		return nil, nil, fmt.Errorf("unknown _op_type %q", in.OpType)
	}
}

// ---------------------------------------------------------------------
// postState: diff against pre for a bounded candidate set (every tx's
// from/to, plus the OP-Stack fee-recipient predeploys), so the vector's
// postState lists every account that actually changed -- never every
// account that merely exists.
// ---------------------------------------------------------------------

type acctSnapshot struct {
	balance *big.Int
	nonce   uint64
	code    []byte
	storage map[common.Hash]common.Hash
}

func snapshotAccounts(pre types.GenesisAlloc, addrs map[common.Address]struct{}) map[common.Address]acctSnapshot {
	out := make(map[common.Address]acctSnapshot, len(addrs))
	for addr := range addrs {
		out[addr] = snapshotOne(pre, addr)
	}
	return out
}

func addMissingSnapshots(dst map[common.Address]acctSnapshot, pre types.GenesisAlloc, addrs map[common.Address]struct{}) {
	for addr := range addrs {
		if _, ok := dst[addr]; !ok {
			dst[addr] = snapshotOne(pre, addr)
		}
	}
}

func snapshotOne(pre types.GenesisAlloc, addr common.Address) acctSnapshot {
	acc, ok := pre[addr]
	if !ok {
		return acctSnapshot{balance: big.NewInt(0)}
	}
	bal := acc.Balance
	if bal == nil {
		bal = big.NewInt(0)
	}
	return acctSnapshot{balance: bal, nonce: acc.Nonce, code: acc.Code, storage: acc.Storage}
}

func diffPostState(statedb *state.StateDB, pre map[common.Address]acctSnapshot) types.GenesisAlloc {
	out := types.GenesisAlloc{}
	addrs := make([]common.Address, 0, len(pre))
	for addr := range pre {
		addrs = append(addrs, addr)
	}
	sort.Slice(addrs, func(i, j int) bool { return addrs[i].Hex() < addrs[j].Hex() })

	for _, addr := range addrs {
		before := pre[addr]
		balance := statedb.GetBalance(addr).ToBig()
		nonce := statedb.GetNonce(addr)
		code := statedb.GetCode(addr)

		changed := balance.Cmp(before.balance) != 0 || nonce != before.nonce || !bytesEqual(code, before.code)
		storage := map[common.Hash]common.Hash{}
		for slot, wantBefore := range before.storage {
			got := statedb.GetState(addr, slot)
			if got != wantBefore {
				changed = true
			}
		}
		// Always re-read the known pre-listed slots into the diff for
		// context (cheap; bounded by what the case file declared).
		for slot := range before.storage {
			storage[slot] = statedb.GetState(addr, slot)
		}
		if !changed {
			continue
		}
		acc := types.Account{Balance: balance, Nonce: nonce}
		if len(code) > 0 {
			acc.Code = code
		}
		if len(storage) > 0 {
			acc.Storage = storage
		}
		out[addr] = acc
	}
	return out
}

func bytesEqual(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// MakePreState builds a fresh in-memory StateDB from a GenesisAlloc. Copied
// verbatim (minus the verkle/bintrie branch, unused by this vector matrix)
// from op-geth cmd/evm/internal/t8ntool/execution.go's MakePreState, which
// is unexported and therefore cannot be imported from outside cmd/evm.
func MakePreState(db ethdb.Database, accounts types.GenesisAlloc) *state.StateDB {
	tdb := triedb.NewDatabase(db, &triedb.Config{Preimages: true})
	sdb := state.NewDatabase(tdb, nil)
	statedb, err := state.New(types.EmptyRootHash, sdb)
	if err != nil {
		panic(fmt.Errorf("failed to create initial statedb: %v", err))
	}
	for addr, a := range accounts {
		statedb.SetCode(addr, a.Code, tracing.CodeChangeUnspecified)
		statedb.SetNonce(addr, a.Nonce, tracing.NonceChangeGenesis)
		statedb.SetBalance(addr, uint256.MustFromBig(a.Balance), tracing.BalanceIncreaseGenesisBalance)
		for k, v := range a.Storage {
			statedb.SetState(addr, k, v)
		}
	}
	root, err := statedb.Commit(0, false, false)
	if err != nil {
		panic(fmt.Errorf("failed to commit initial state: %v", err))
	}
	statedb, err = state.New(root, sdb)
	if err != nil {
		panic(fmt.Errorf("failed to reopen state after commit: %v", err))
	}
	return statedb
}
