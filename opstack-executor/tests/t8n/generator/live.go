// Command opt8n-ref --live — real-chain (OP Sepolia) replay vector generator.
//
// Opposite of the synthetic --input path: instead of building blocks from in-Go
// case definitions, it pulls REAL blocks from an OP Sepolia node (op-geth, debug
// API enabled) and re-executes them inside this op-geth checkout through the same
// library pipeline the corpus uses (core.StateProcessor.Process == the
// ApplyTransaction + system-call machinery behind InsertChain). The outputs are
// v3-block chain vectors (one vector id, `blocks` array) that the
// op-mainnet-replay CLI feeds through ReplayGate.h.
//
//   opt8n-ref --live <rpc> --from <h0> [--to latest|<h1>|--count <n>]
//       --out <chain.json> --sidecar <state.sidecar> --op-geth-commit <sha>
//       [--fork jovian|isthmus]
//
// Data flow (contracts pinned to the plan / superpowers spec):
//  1. Chain config from the embedded superchain registry (chainId 11155420).
//     The window must lie fully inside (jovian, karst): all replayed timestamps
//     are validated, and the pinned registry has NO karst_time, so the "Jovian
//     semantics only" guarantee is structural.
//  2. State bootstrap at h0-1 via debug_accountRange streaming pagination
//     (256 accounts/call, `next` cursor) into a fresh memory DB + sidecar
//     (MAGIC v1 / ROOT / per-account lines — the loadDumpSidecar contract).
//     The committed state root MUST equal the real h0-1 header stateRoot;
//     a mismatch aborts (incomplete export would poison every replayed block).
//  3. BLOCKHASH pre-fill: [h0-256, h0-1] number->hash written to
//     _op_block_hashes (ReplayOptions.blockHashes on the CLI side).
//  4. Blocks [h0..h1] from eth_getBlockByNumber (full tx objects; deposit txs
//     included — op-geth's tx JSON round-trips type 0x7e) + raw envelopes via
//     tx.MarshalBinary; assembled with types.NewBlock over the REAL headers.
//  5. Golden-standard cross-check: each block is run through
//     core.NewStateProcessor.Process from its real parent root; the resulting
//     state root, gasUsed, receipts root and logsBloom must EQUAL the real
//     header's — the ValidateState contract. ANY divergence errors out; a
//     vector is never emitted for an execution that does not reproduce the
//     real headers.
//  6. Per-block emission: env/block/_op_expected from the REAL header + the
//     Process-derived receipts (+ per-tx return data via a header-serving
//     re-execution replay; operator-fee fields from the post-deposit L1Block
//     slot 8). No pre/postState on live vectors (state comes from the sidecar;
//     full-state equality rides the stateRoot field compare).
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"math/big"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/common/hexutil"
	"github.com/ethereum/go-ethereum/common/math"
	"github.com/ethereum/go-ethereum/consensus"
	"github.com/ethereum/go-ethereum/consensus/beacon"
	"github.com/ethereum/go-ethereum/consensus/ethash"
	"github.com/ethereum/go-ethereum/core"
	"github.com/ethereum/go-ethereum/core/rawdb"
	"github.com/ethereum/go-ethereum/core/state"
	"github.com/ethereum/go-ethereum/core/tracing"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/core/vm"
	"github.com/ethereum/go-ethereum/ethdb"
	"github.com/ethereum/go-ethereum/ethclient"
	"github.com/ethereum/go-ethereum/params"
	"github.com/ethereum/go-ethereum/rpc"
	"github.com/ethereum/go-ethereum/superchain"
	"github.com/ethereum/go-ethereum/triedb"
	"github.com/ethereum/go-ethereum/trie"
	"github.com/holiman/uint256"
)

// OP Sepolia window anchors (verified against the pinned superchain registry).
const (
	liveChainID = 11155420
	liveJovian  = 1763568001
	liveKarst   = 1781712001
	liveIsthmus = 1744905600
)

// liveAccountRange mirrors the op-geth debug_accountRange result (state.Dump).
type liveAccountRange struct {
	Accounts map[string]state.DumpAccount `json:"accounts"`
	Next     hexutil.Bytes                `json:"next"`
}

// liveChainCtx serves the EVM's historical-header reads (BLOCKHASH) from the
// prefetched window + pre-fill headers, so Process and the output re-execution
// are faithful for contracts that read block hashes (unlike the corpus
// chainCtxStub). Engine() must be non-nil: StateProcessor.Process calls
// Engine().Finalize at the end of every block.
type liveChainCtx struct {
	cfg    *params.ChainConfig
	byNum  map[uint64]*types.Header
	byHash map[common.Hash]*types.Header
}

func (c liveChainCtx) Engine() consensus.Engine           { return beacon.New(ethash.NewFaker()) }
func (c liveChainCtx) Config() *params.ChainConfig        { return c.cfg }
func (c liveChainCtx) CurrentHeader() *types.Header       { return nil }
func (c liveChainCtx) GetHeader(hash common.Hash, _ uint64) *types.Header {
	return c.byHash[hash]
}
func (c liveChainCtx) GetHeaderByNumber(n uint64) *types.Header { return c.byNum[n] }
func (c liveChainCtx) GetHeaderByHash(hash common.Hash) *types.Header {
	return c.byHash[hash]
}

// liveStateGetter adapts a (root, state.Database) pair to types.StateGetter for
// the operator-cost cross-check (slot-8 params read from the same state the
// emitted receipts claim).
type liveStateGetter struct {
	root common.Hash
	db   state.Database
}

func (g liveStateGetter) GetState(addr common.Address, slot common.Hash) common.Hash {
	sd, err := state.New(g.root, g.db)
	if err != nil {
		return common.Hash{}
	}
	return sd.GetState(addr, slot)
}

// liveReexecuteOutputs is the live twin of reexecuteOutputs: replays a real
// block's txs against the pre-block state (EIP-2935/4788 system calls first) to
// capture per-tx return data, serving BLOCKHASH from the real headers. Same
// per-tx faithfulness gate (gasUsed/status must equal the Process receipts).
func liveReexecuteOutputs(ctx context.Context, cfg *params.ChainConfig, db ethdb.Database, hc liveChainCtx,
	header *types.Header, txs []*types.Transaction, receipts types.Receipts,
	startRoot common.Hash) ([][]byte, error) {
	tdb := triedb.NewDatabase(db, triedb.HashDefaults)
	defer tdb.Close()
	statedb, err := state.New(startRoot, state.NewDatabase(tdb, nil))
	if err != nil {
		return nil, fmt.Errorf("open re-execution state: %w", err)
	}
	gp := core.NewGasPool(header.GasLimit)
	coinbase := header.Coinbase

	if cfg.IsPrague(header.Number, header.Time) || cfg.IsVerkle(header.Number, header.Time) {
		blockCtx := core.NewEVMBlockContext(header, hc, &coinbase, cfg, statedb)
		blockCtx.Random = &common.Hash{}
		core.ProcessParentBlockHash(header.ParentHash, vm.NewEVM(blockCtx, statedb, cfg, vm.Config{}))
	}
	blockCtx := core.NewEVMBlockContext(header, hc, &coinbase, cfg, statedb)
	if header.ParentBeaconRoot != nil {
		core.ProcessBeaconBlockRoot(*header.ParentBeaconRoot, vm.NewEVM(blockCtx, statedb, cfg, vm.Config{}))
	}
	outs := make([][]byte, len(txs))
	for i, tx := range txs {
		statedb.SetTxContext(tx.Hash(), i)
		blockCtx := core.NewEVMBlockContext(header, hc, &coinbase, cfg, statedb)
		evm := vm.NewEVM(blockCtx, statedb, cfg, vm.Config{})
		msg, err := core.TransactionToMessage(tx, types.MakeSigner(cfg, header.Number, header.Time), header.BaseFee)
		if err != nil {
			return nil, fmt.Errorf("replay tx %d msg: %w", i, err)
		}
		result, err := core.ApplyMessage(evm, msg, gp)
		if err != nil {
			return nil, fmt.Errorf("replay tx %d: %w", i, err)
		}
		if result.UsedGas != receipts[i].GasUsed ||
			result.Failed() != (receipts[i].Status != types.ReceiptStatusSuccessful) {
			return nil, fmt.Errorf("replay tx %d: gasUsed/status mismatch (replay %d/%v vs receipt %d/%d)",
				i, result.UsedGas, result.Failed(), receipts[i].GasUsed, receipts[i].Status)
		}
		outs[i] = common.CopyBytes(result.ReturnData)
		statedb.Finalise(true)
	}
	return outs, nil
}

// buildLiveExpectedReceipts mirrors buildExpectedReceipts with the operator-fee
// params read from the POST-deposit L1Block slot 8 (the state the block's normal
// txs were actually charged against — the corpus's pre-state shortcut does not
// hold for real chains where the L1 attributes deposit rewrites slot 8).
func buildLiveExpectedReceipts(cfg *params.ChainConfig, sdb state.Database,
	txs []*types.Transaction, receipts types.Receipts, outputs [][]byte, blockTime uint64,
	postRoot common.Hash) ([]expectedReceipt, error) {
	jovian := cfg.IsJovian(blockTime)
	if len(outputs) != len(receipts) {
		return nil, fmt.Errorf("outputs/receipts count mismatch: %d vs %d", len(outputs), len(receipts))
	}
	sd, err := state.New(postRoot, sdb)
	if err != nil {
		return nil, fmt.Errorf("open post-block state for fee params: %w", err)
	}
	slot8 := sd.GetState(l1BlockAddr, types.OperatorFeeParamsSlot)
	opScalar := binaryBigEndianUint32(slot8[20:24])
	opConstant := binaryBigEndianUint64(slot8[24:32])
	emitOpFee := opScalar != 0 || opConstant != 0
	refOpCost := types.NewOperatorCostFunc(cfg, liveStateGetter{root: postRoot, db: sdb})

	out := make([]expectedReceipt, len(receipts))
	for i, r := range receipts {
		er := expectedReceipt{
			Type:              hexutil.EncodeUint64(uint64(r.Type)),
			Status:            hexutil.EncodeUint64(r.Status),
			GasUsed:           hexutil.EncodeUint64(r.GasUsed),
			CumulativeGasUsed: hexutil.EncodeUint64(r.CumulativeGasUsed),
			LogsCount:         len(r.Logs),
			Output:            hexutil.Encode(outputs[i]),
		}
		if r.DepositNonce != nil {
			s := hexutil.EncodeUint64(*r.DepositNonce)
			er.OpDepositNonce = &s
		}
		if r.DepositReceiptVersion != nil {
			s := hexutil.EncodeUint64(*r.DepositReceiptVersion)
			er.OpDepositReceiptVersion = &s
		}
		if !txs[i].IsDepositTx() {
			if r.L1Fee == nil {
				return nil, fmt.Errorf("receipt %d: non-deposit tx missing L1Fee", i)
			}
			s := hexutil.EncodeBig(r.L1Fee)
			er.OpL1Fee = &s
			if r.L1GasPrice != nil {
				s := hexutil.EncodeBig(r.L1GasPrice)
				er.OpL1GasPrice = &s
			}
			if r.L1BlobBaseFee != nil {
				s := hexutil.EncodeBig(r.L1BlobBaseFee)
				er.OpL1BlobBaseFee = &s
			}
			if r.L1GasUsed != nil {
				s := hexutil.EncodeBig(r.L1GasUsed)
				er.OpL1GasUsed = &s
			}
			if r.L1BaseFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.L1BaseFeeScalar)
				er.OpL1BaseFeeScalar = &s
			}
			if r.L1BlobBaseFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.L1BlobBaseFeeScalar)
				er.OpL1BlobBaseFeeScalar = &s
			}
			if r.OperatorFeeScalar != nil {
				s := hexutil.EncodeUint64(*r.OperatorFeeScalar)
				er.OpOperatorFeeScalar = &s
			}
			if r.OperatorFeeConstant != nil {
				s := hexutil.EncodeUint64(*r.OperatorFeeConstant)
				er.OpOperatorFeeConstant = &s
			}
			if r.DAFootprintGasScalar != nil {
				s := hexutil.EncodeUint64(*r.DAFootprintGasScalar)
				er.OpDaFootprintGasScalar = &s
			}
			if emitOpFee {
				fee := operatorFee(jovian, r.GasUsed, opScalar, opConstant)
				if ref := refOpCost(r.GasUsed, blockTime); ref.ToBig().Cmp(fee) != 0 {
					return nil, fmt.Errorf("receipt %d: operator fee formula mismatch: generator %s vs op-geth %s", i, fee, ref)
				}
				sf := hexutil.EncodeBig(fee)
				er.OpOperatorFee = &sf
			}
			if jovian {
				sd := hexutil.EncodeUint64(r.BlobGasUsed)
				er.OpDaFootprint = &sd
			}
		}
		out[i] = er
	}
	return out, nil
}

// liveTxToOutput converts a real tx into the replayer's block-transaction arm
// (deposit / eip1559 / setcode / legacy), with _op_raw = the raw EIP-2718
// envelope and structured fields decoded from the tx (exactly what the FISCO
// loader feeds processOpBlock, so the two sides execute the same transaction).
func liveTxToOutput(tx *types.Transaction, signer types.Signer) (json.RawMessage, error) {
	raw, err := tx.MarshalBinary()
	if err != nil {
		return nil, fmt.Errorf("tx %s MarshalBinary: %w", tx.Hash(), err)
	}
	rawHex := hexutil.Encode(raw)
	switch tx.Type() {
	case types.DepositTxType:
		// MarshalJSON round-trips the 0x7e deposit fields (rpc tx shape); the
		// replayer expects the snake_case deposit struct.
		rt, err := txRpcJSON(tx)
		if err != nil {
			return nil, err
		}
		dep := inputDeposit{
			From:       common.HexToAddress(rtStr(rt, "from")),
			To:         rtOptAddr(rt, "to"),
			Mint:       rtOptBig(rt, "mint"),
			Value:      rtOptBig(rt, "value"),
			Gas:        math.HexOrDecimal64(rtUint64(rt, "gas")),
			IsSystemTx: rtBool(rt, "isSystemTx"),
			SourceHash: common.HexToHash(rtStr(rt, "sourceHash")),
		}
		return json.Marshal(outputDepositTx{
			OpType:    "deposit",
			OpDeposit: dep,
			Data:      hexutil.Bytes(tx.Data()),
		})

	case types.DynamicFeeTxType:
		from, err := types.Sender(signer, tx)
		if err != nil {
			return nil, fmt.Errorf("tx %s sender: %w", tx.Hash(), err)
		}
		return json.Marshal(outputSignedTx{
			OpType:               "eip1559",
			OpRaw:                rawHex,
			ChainID:              (*math.HexOrDecimal256)(tx.ChainId()),
			Nonce:                math.HexOrDecimal64(tx.Nonce()),
			To:                   tx.To(),
			Gas:                  math.HexOrDecimal64(tx.Gas()),
			MaxFeePerGas:         (*math.HexOrDecimal256)(tx.GasFeeCap()),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tx.GasTipCap()),
			Value:                (*math.HexOrDecimal256)(tx.Value()),
			Data:                 hexutil.Bytes(tx.Data()),
			AccessList:           liveAccessList(tx.AccessList()),
			Sender:               from,
		})

	case types.SetCodeTxType:
		from, err := types.Sender(signer, tx)
		if err != nil {
			return nil, fmt.Errorf("tx %s sender: %w", tx.Hash(), err)
		}
		rt, err := txRpcJSON(tx)
		if err != nil {
			return nil, err
		}
		var auths []outputAuthorization
		if al, ok := rt["authorizationList"].([]interface{}); ok {
			for _, rawAuth := range al {
				m, ok := rawAuth.(map[string]interface{})
				if !ok {
					return nil, fmt.Errorf("tx %s: malformed authorizationList entry", tx.Hash())
				}
				auths = append(auths, outputAuthorization{
					ChainID: (*math.HexOrDecimal256)(rtBig(m, "chainId")),
					Address: common.HexToAddress(rtStr(m, "address")),
					Nonce:   math.HexOrDecimal64(rtUint64(m, "nonce")),
					YParity: math.HexOrDecimal64(rtUint64(m, "yParity")),
					R:       (*math.HexOrDecimal256)(rtBig(m, "r")),
					S:       (*math.HexOrDecimal256)(rtBig(m, "s")),
				})
			}
		}
		if len(auths) == 0 {
			return nil, fmt.Errorf("tx %s: setcode tx with empty authorization list", tx.Hash())
		}
		return json.Marshal(outputSetCodeTx{
			OpType:               "setcode",
			OpRaw:                rawHex,
			ChainID:              (*math.HexOrDecimal256)(tx.ChainId()),
			Nonce:                math.HexOrDecimal64(tx.Nonce()),
			To:                   *tx.To(),
			Gas:                  math.HexOrDecimal64(tx.Gas()),
			MaxFeePerGas:         (*math.HexOrDecimal256)(tx.GasFeeCap()),
			MaxPriorityFeePerGas: (*math.HexOrDecimal256)(tx.GasTipCap()),
			Value:                (*math.HexOrDecimal256)(tx.Value()),
			Data:                 hexutil.Bytes(tx.Data()),
			AccessList:           liveAccessList(tx.AccessList()),
			Sender:               from,
			OpAuthorizationList:  auths,
		})

	case types.LegacyTxType:
		from, err := types.Sender(signer, tx)
		if err != nil {
			return nil, fmt.Errorf("tx %s sender: %w", tx.Hash(), err)
		}
		return json.Marshal(outputLegacyTx{
			OpType:   "legacy",
			OpRaw:    rawHex,
			ChainID:  (*math.HexOrDecimal256)(tx.ChainId()),
			Nonce:    math.HexOrDecimal64(tx.Nonce()),
			To:       tx.To(),
			Gas:      math.HexOrDecimal64(tx.Gas()),
			GasPrice: (*math.HexOrDecimal256)(tx.GasPrice()),
			Value:    (*math.HexOrDecimal256)(tx.Value()),
			Data:     hexutil.Bytes(tx.Data()),
			Sender:   from,
		})

	default:
		return nil, fmt.Errorf("tx %s: unsupported live tx type %d (blob txs are rejected on OP chains)", tx.Hash(), tx.Type())
	}
}

// runLive implements the --live subcommand (see file header).
func runLive(rpcURL string, from uint64, toSpec string, count uint64,
	outPath, sidecarPath, commit, fork string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 45*time.Minute)
	defer cancel()
	rc, err := rpc.DialContext(ctx, rpcURL)
	if err != nil {
		return fmt.Errorf("rpc dial %s: %w", rpcURL, err)
	}
	defer rc.Close()
	ec := ethclient.NewClient(rc)

	// --- chain config: real Sepolia schedule over the corpus base template ---
	// (values verified against the pinned superchain registry, configs/sepolia/op.toml;
	// the pin predates Karst, so KarstTime stays nil — the window check below enforces
	// the pre-Karst bound structurally).
	sc, ok := superchain.Chains[liveChainID]
	if !ok {
		return fmt.Errorf("superchain registry has no chain %d", liveChainID)
	}
	scCfg, err := sc.Config()
	if err != nil {
		return fmt.Errorf("superchain config: %w", err)
	}
	cfg := liveChainConfig(scCfg)
	nodeChainID, err := ec.ChainID(ctx)
	if err != nil {
		return fmt.Errorf("eth_chainId: %w", err)
	}
	if nodeChainID.Uint64() != liveChainID {
		return fmt.Errorf("node chain id %d != %d (OP Sepolia)", nodeChainID.Uint64(), liveChainID)
	}

	// --- window resolution + fork validation ---
	if from == 0 {
		return errors.New("--from must be >= 1 (state bootstrapped at from-1)")
	}
	var toNum uint64
	switch {
	case count > 0:
		toNum = from + count - 1 // direct arithmetic: never round-trip through a string
	case toSpec == "" || toSpec == "latest":
		latest, err := ec.BlockNumber(ctx)
		if err != nil {
			return fmt.Errorf("eth_blockNumber: %w", err)
		}
		toNum = latest
	default:
		// --to <h1>: decimal unless 0x-prefixed (consistent with flag base-0 parsing).
		numStr, base := toSpec, 10
		if strings.HasPrefix(numStr, "0x") {
			numStr, base = numStr[2:], 16
		}
		v, err := strconv.ParseUint(numStr, base, 64)
		if err != nil {
			return fmt.Errorf("bad --to %q", toSpec)
		}
		toNum = v
	}
	if toNum < from {
		return fmt.Errorf("window end %d < start %d", toNum, from)
	}
	h0hdr, err := ec.HeaderByNumber(ctx, big.NewInt(int64(from)))
	if err != nil {
		return fmt.Errorf("header %d: %w", from, err)
	}
	h1hdr, err := ec.HeaderByNumber(ctx, big.NewInt(int64(toNum)))
	if err != nil {
		return fmt.Errorf("header %d: %w", toNum, err)
	}
	lower, lowerName := uint64(0), ""
	switch fork {
	case "isthmus":
		lower, lowerName = liveIsthmus, "isthmus"
	case "jovian", "":
		lower, lowerName = liveJovian, "jovian"
	default:
		return fmt.Errorf("--fork must be jovian|isthmus, got %q", fork)
	}
	if h0hdr.Time <= lower || h1hdr.Time >= liveKarst {
		return fmt.Errorf("window [%d,%d] timestamps (%d..%d) must lie in (%d,%d) for fork %s (karst pre-window)",
			from, toNum, h0hdr.Time, h1hdr.Time, lower, liveKarst, lowerName)
	}
	fmt.Printf("live window: blocks %d..%d, timestamps %d..%d, fork %s, chainId %d\n",
		from, toNum, h0hdr.Time, h1hdr.Time, lowerName, liveChainID)

	// --- state bootstrap (h0-1) via debug_accountRange streaming ---
	anchorHdr, err := ec.HeaderByNumber(ctx, big.NewInt(int64(from-1)))
	if err != nil {
		return fmt.Errorf("anchor header %d: %w", from-1, err)
	}
	db := rawdb.NewMemoryDatabase()
	tdb := triedb.NewDatabase(db, triedb.HashDefaults)
	defer tdb.Close()
	sdb := state.NewDatabase(tdb, nil)
	statedb, err := state.New(common.Hash{}, sdb)
	if err != nil {
		return fmt.Errorf("bootstrap state: %w", err)
	}
	sidecar, err := os.Create(sidecarPath)
	if err != nil {
		return fmt.Errorf("sidecar create: %w", err)
	}
	defer sidecar.Close()
	// ROOT = the real h0-1 header root (known upfront); the committed bootstrap
	// root must equal it, making the sidecar self-verifying on the CLI side.
	fmt.Fprintln(sidecar, "MAGIC v1")
	fmt.Fprintf(sidecar, "ROOT %s\n", anchorHdr.Root)

	startKey := hexutil.Bytes{}
	accountCount := 0
	for {
		var dump liveAccountRange
		callCtx, cc := context.WithTimeout(ctx, 2*time.Minute)
		err := rc.CallContext(callCtx, &dump, "debug_accountRange",
			rpc.BlockNumberOrHashWithHash(anchorHdr.Hash(), false), startKey, 256, false, false, false)
		cc()
		if err != nil {
			return fmt.Errorf("debug_accountRange: %w", err)
		}
		if len(dump.Accounts) == 0 && len(dump.Next) == 0 {
			break
		}
		for addrStr, acc := range dump.Accounts {
			addr := common.HexToAddress(addrStr)
			balance, ok := new(big.Int).SetString(strings.TrimPrefix(acc.Balance, "0x"), 16)
			if !ok {
				return fmt.Errorf("account %s: bad balance %q", addrStr, acc.Balance)
			}
			statedb.SetBalance(addr, uint256.MustFromBig(balance), tracing.BalanceChangeUnspecified)
			statedb.SetNonce(addr, acc.Nonce, tracing.NonceChangeUnspecified)
			if len(acc.Code) > 0 {
				statedb.SetCode(addr, acc.Code, tracing.CodeChangeUnspecified)
			}
			// sidecar line: <addr> <balance> <nonce> <codeHex|-> <storageCount> [<slot> <val>]...
			codeHex := "-"
			if len(acc.Code) > 0 {
				codeHex = hexutil.Encode(acc.Code)
			}
			fmt.Fprintf(sidecar, "%s %s %s %s %d",
				addr.Hex(), hexutil.EncodeBig(balance), hexutil.EncodeUint64(acc.Nonce), codeHex, len(acc.Storage))
			for slot, valStr := range acc.Storage {
				val, ok := new(big.Int).SetString(strings.TrimPrefix(valStr, "0x"), 16)
				if !ok {
					return fmt.Errorf("account %s: bad storage value %q", addrStr, valStr)
				}
				statedb.SetState(addr, slot, common.BigToHash(val))
				fmt.Fprintf(sidecar, " %s %s", slot.Hex(), hexutil.EncodeBig(val))
			}
			fmt.Fprintln(sidecar)
			accountCount++
		}
		if len(dump.Next) == 0 {
			break
		}
		startKey = dump.Next
	}
	root, err := statedb.Commit(0, true, false)
	if err != nil {
		return fmt.Errorf("bootstrap commit: %w", err)
	}
	if root != anchorHdr.Root {
		return fmt.Errorf("state export incomplete: bootstrapped root %s != real h%d root %s",
			root, from-1, anchorHdr.Root)
	}
	fmt.Printf("state bootstrap: %d accounts, root %s == real header root\n", accountCount, root)

	// --- BLOCKHASH pre-fill [from-256, from-1] + header serving maps ---
	headerByNum := make(map[uint64]*types.Header)
	headerByHash := make(map[common.Hash]*types.Header)
	headerByHash[anchorHdr.Hash()] = anchorHdr
	headerByNum[from-1] = anchorHdr
	prefill := make(map[string]string)
	low := from - 256
	if low < 1 {
		low = 1
	}
	for h := low; h <= from-1; h++ {
		hdr, err := ec.HeaderByNumber(ctx, big.NewInt(int64(h)))
		if err != nil {
			return fmt.Errorf("prefill header %d: %w", h, err)
		}
		prefill[fmt.Sprintf("%d", h)] = hdr.Hash().Hex()
		headerByNum[h] = hdr
		headerByHash[hdr.Hash()] = hdr
	}
	fmt.Printf("blockhash prefill: [%d,%d] (%d entries)\n", low, from-1, len(prefill))

	// --- fetch + assemble real blocks [from..to] ---
	var blocks []*types.Block
	for h := from; h <= toNum; h++ {
		blk, err := ec.BlockByNumber(ctx, big.NewInt(int64(h)))
		if err != nil {
			return fmt.Errorf("block %d: %w", h, err)
		}
		headerByNum[h] = blk.Header()
		headerByHash[blk.Hash()] = blk.Header()
		blocks = append(blocks, blk)
	}
	fmt.Printf("fetched %d blocks (%d..%d)\n", len(blocks), from, toNum)

	// --- golden-standard cross-check: StateProcessor.Process per block ---
	// The real h0-1 root is the parent of block h0; each block's Process runs
	// against its real parent root and the produced state root / gasUsed /
	// receipts root / logsBloom must equal the real header's (ValidateState).
	processor := core.NewStateProcessor(liveChainCtx{cfg: cfg, byNum: headerByNum, byHash: headerByHash})
	parentRoot := anchorHdr.Root
	blockReceipts := make([]types.Receipts, len(blocks))
	for i, blk := range blocks {
		blkStatedb, err := state.New(parentRoot, sdb)
		if err != nil {
			return fmt.Errorf("block %d: open parent state: %w", blk.NumberU64(), err)
		}
		result, err := processor.Process(ctx, blk, blkStatedb, vm.Config{})
		if err != nil {
			return fmt.Errorf("block %d Process (cross-check FAILED): %w", blk.NumberU64(), err)
		}
		if root := blkStatedb.IntermediateRoot(true); root != blk.Root() {
			return fmt.Errorf("block %d: stateRoot mismatch: executed %s vs real %s", blk.NumberU64(), root, blk.Root())
		}
		if result.GasUsed != blk.GasUsed() {
			return fmt.Errorf("block %d: gasUsed mismatch: executed %d vs real %d", blk.NumberU64(), result.GasUsed, blk.GasUsed())
		}
		if rr := types.DeriveSha(result.Receipts, trie.NewStackTrie(nil)); rr != blk.ReceiptHash() {
			return fmt.Errorf("block %d: receiptsRoot mismatch: executed %s vs real %s", blk.NumberU64(), rr, blk.ReceiptHash())
		}
		if bloom := types.MergeBloom(result.Receipts); bloom != blk.Bloom() {
			return fmt.Errorf("block %d: logsBloom mismatch", blk.NumberU64())
		}
		if _, err := blkStatedb.Commit(blk.NumberU64(), true, false); err != nil {
			return fmt.Errorf("block %d: state commit: %w", blk.NumberU64(), err)
		}
		blockReceipts[i] = result.Receipts
		parentRoot = blk.Root()
	}
	fmt.Printf("header cross-check OK ×%d (Process + stateRoot/gasUsed/receiptsRoot/bloom)\n", len(blocks))

	// --- emit the chain vector ---
	hc := liveChainCtx{cfg: cfg, byNum: headerByNum, byHash: headerByHash}
	id := strings.TrimSuffix(filepath.Base(outPath), filepath.Ext(outPath))
	vec := liveVectorOut{Blocks: make([]liveBlockOut, 0, len(blocks))}
	for i, blk := range blocks {
		hdr := blk.Header()
		signer := types.MakeSigner(cfg, hdr.Number, hdr.Time)
		receipts := blockReceipts[i]
		if len(receipts) != len(blk.Transactions()) {
			return fmt.Errorf("block %d: receipts/tx count mismatch %d vs %d",
				hdr.Number, len(receipts), len(blk.Transactions()))
		}
		parentRoot := anchorHdr.Root
		if i > 0 {
			parentRoot = blocks[i-1].Root()
		}
		outs, err := liveReexecuteOutputs(ctx, cfg, db, hc, hdr, blk.Transactions(), receipts, parentRoot)
		if err != nil {
			return fmt.Errorf("block %d outputs: %w", hdr.Number.Uint64(), err)
		}
		exp, err := buildLiveExpectedReceipts(cfg, sdb, blk.Transactions(), receipts, outs, hdr.Time, blk.Root())
		if err != nil {
			return fmt.Errorf("block %d expected receipts: %w", hdr.Number.Uint64(), err)
		}
		outTxs := make([]json.RawMessage, 0, len(blk.Transactions()))
		for _, tx := range blk.Transactions() {
			o, err := liveTxToOutput(tx, signer)
			if err != nil {
				return fmt.Errorf("block %d: %w", hdr.Number.Uint64(), err)
			}
			outTxs = append(outTxs, o)
		}
		vec.Blocks = append(vec.Blocks, liveBlockOut{
			Info: caseInfo{Hardfork: fork, Description: "OP Sepolia live block " + hdr.Number.String()},
			Env:  liveEnvFromHeader(hdr),
			Block: outputBlock{
				Transactions: outTxs,
			},
			OpExpected: opExpected{
				Header:   liveExpectedHeader(hdr),
				Receipts: exp,
			},
		})
	}

	meta := map[string]any{
		"version":          schemaVersion,
		"generator_commit": commit,
		"network":          "op-sepolia",
		"chain_id":         liveChainID,
		"from":             from,
		"to":               toNum,
	}
	doc := map[string]any{
		"_op_test_vectors": meta,
		"_op_block_hashes": prefill,
		id:                 vec,
	}
	raw, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal vector: %w", err)
	}
	if err := os.WriteFile(outPath, raw, 0o644); err != nil {
		return fmt.Errorf("write %s: %w", outPath, err)
	}
	fmt.Printf("wrote %s (vector id %q, %d blocks) + %s\n", outPath, id, len(vec.Blocks), sidecarPath)
	return nil
}

// liveChainConfig builds the OP Sepolia params.ChainConfig from the pinned
// superchain registry's parsed chain config. Mirrors buildChainConfigSpec's
// ETH-twin coupling (canyon->shanghai, ecotone->cancun, isthmus->prague —
// CheckOptimismValidity requires them).
func liveChainConfig(sc *superchain.ChainConfig) *params.ChainConfig {
	conf := *params.OptimismTestConfig
	conf.ChainID = big.NewInt(liveChainID)
	conf.RegolithTime = uint64Ptr(0)
	if v := sc.Hardforks.CanyonTime; v != nil {
		conf.CanyonTime = v
		conf.ShanghaiTime = v // ETH twin
	}
	if v := sc.Hardforks.EcotoneTime; v != nil {
		conf.EcotoneTime = v
		conf.CancunTime = v // ETH twin
	}
	if v := sc.Hardforks.FjordTime; v != nil {
		conf.FjordTime = v
	}
	if v := sc.Hardforks.GraniteTime; v != nil {
		conf.GraniteTime = v
	}
	if v := sc.Hardforks.HoloceneTime; v != nil {
		conf.HoloceneTime = v
	}
	if v := sc.Hardforks.IsthmusTime; v != nil {
		conf.IsthmusTime = v
		conf.PragueTime = v // ETH twin
	}
	if v := sc.Hardforks.JovianTime; v != nil {
		conf.JovianTime = v
	}
	conf.KarstTime = nil   // pin predates Karst; the window check enforces the bound
	conf.InteropTime = nil // not part of this stack's window
	if sc.Optimism != nil {
		conf.Optimism = &params.OptimismConfig{
			EIP1559Elasticity:        sc.Optimism.EIP1559Elasticity,
			EIP1559Denominator:       sc.Optimism.EIP1559Denominator,
			EIP1559DenominatorCanyon: sc.Optimism.EIP1559DenominatorCanyon,
		}
	}
	return &conf
}

// liveVectorOut is the on-disk shape of one live chain vector (blocks array).
type liveVectorOut struct {
	Blocks []liveBlockOut `json:"blocks"`
}

type liveBlockOut struct {
	Info       caseInfo    `json:"_info"`
	Env        outputEnv   `json:"env"`
	Block      outputBlock `json:"block"`
	OpExpected opExpected  `json:"_op_expected"`
}

func liveEnvFromHeader(hdr *types.Header) outputEnv {
	env := outputEnv{
		CurrentCoinbase:       hdr.Coinbase.Hex(),
		CurrentNumber:         hexutil.EncodeUint64(hdr.Number.Uint64()),
		CurrentTimestamp:      hexutil.EncodeUint64(hdr.Time),
		CurrentGasLimit:       hexutil.EncodeUint64(hdr.GasLimit),
		CurrentBaseFee:        hexutil.EncodeBig(hdr.BaseFee),
		CurrentRandom:         hdr.MixDigest.Hex(),
		ParentHash:            hdr.ParentHash.Hex(),
		ParentBeaconBlockRoot: common.Hash{}.Hex(),
	}
	if hdr.ParentBeaconRoot != nil {
		env.ParentBeaconBlockRoot = hdr.ParentBeaconRoot.Hex()
	}
	return env
}

func liveExpectedHeader(hdr *types.Header) expectedHeader {
	exp := expectedHeader{
		GasUsed:         hexutil.EncodeUint64(hdr.GasUsed),
		ReceiptsRoot:    hdr.ReceiptHash.Hex(),
		LogsBloom:       hexutil.Encode(hdr.Bloom[:]),
		WithdrawalsRoot: hdr.WithdrawalsHash.Hex(),
		StateRoot:       hdr.Root.Hex(),
		BlobGasUsed:     hexutil.EncodeUint64(0), // Jovian: DA-footprint field; zero if absent
	}
	if hdr.BlobGasUsed != nil {
		exp.BlobGasUsed = hexutil.EncodeUint64(*hdr.BlobGasUsed)
	}
	return exp
}

func liveAccessList(al types.AccessList) []outputAccessTuple {
	out := make([]outputAccessTuple, 0, len(al))
	for _, e := range al {
		keys := make([]common.Hash, 0, len(e.StorageKeys))
		keys = append(keys, e.StorageKeys...)
		out = append(out, outputAccessTuple{Address: e.Address, StorageKeys: keys})
	}
	return out
}

// txRpcJSON marshals a tx to its RPC JSON shape (op-geth's MarshalJSON handles
// deposit 0x7e + setcode authorizationList).
func txRpcJSON(tx *types.Transaction) (map[string]interface{}, error) {
	b, err := tx.MarshalJSON()
	if err != nil {
		return nil, fmt.Errorf("tx %s MarshalJSON: %w", tx.Hash(), err)
	}
	var m map[string]interface{}
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("tx %s JSON decode: %w", tx.Hash(), err)
	}
	return m, nil
}

func rtStr(m map[string]interface{}, key string) string {
	if s, ok := m[key].(string); ok {
		return s
	}
	return ""
}

func rtBool(m map[string]interface{}, key string) bool {
	b, _ := m[key].(bool)
	return b
}

func rtUint64(m map[string]interface{}, key string) uint64 {
	s := rtStr(m, key)
	v, _ := new(big.Int).SetString(strings.TrimPrefix(s, "0x"), 16)
	if v == nil {
		return 0
	}
	return v.Uint64()
}

func rtBig(m map[string]interface{}, key string) *big.Int {
	s := rtStr(m, key)
	v, _ := new(big.Int).SetString(strings.TrimPrefix(s, "0x"), 16)
	if v == nil {
		return new(big.Int)
	}
	return v
}

func rtOptAddr(m map[string]interface{}, key string) *common.Address {
	s := rtStr(m, key)
	if s == "" || s == "0x" {
		return nil
	}
	a := common.HexToAddress(s)
	return &a
}

func rtOptBig(m map[string]interface{}, key string) *math.HexOrDecimal256 {
	v := rtBig(m, key)
	if v.Sign() == 0 {
		return nil // corpus convention: mint/value 0 omitted
	}
	return (*math.HexOrDecimal256)(v)
}

func binaryBigEndianUint32(b []byte) uint32 {
	if len(b) < 4 {
		return 0
	}
	return uint32(b[0])<<24 | uint32(b[1])<<16 | uint32(b[2])<<8 | uint32(b[3])
}

func binaryBigEndianUint64(b []byte) uint64 {
	if len(b) < 8 {
		return 0
	}
	var v uint64
	for _, x := range b[:8] {
		v = v<<8 | uint64(x)
	}
	return v
}

