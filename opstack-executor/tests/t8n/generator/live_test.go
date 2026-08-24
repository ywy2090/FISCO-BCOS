// live_test.go — unit tests for the --live pure-function layer (no node needed).
//
// Pins the review-found live-path defects: requestsHash emission
// (liveExpectedHeader) and the decimal balance parse (parseDumpBalance), plus
// the Sepolia chain-config mapping (liveChainConfig).
package main

import (
	"math/big"
	"strings"
	"testing"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/params"
	"github.com/ethereum/go-ethereum/superchain"
)

func TestLiveExpectedHeaderRequestsHash(t *testing.T) {
	// Isthmus+ blocks carry requestsHash (sha256("")); the corpus generator and
	// FISCO sealOpBlock both emit it — a live vector without it DIVERGEs.
	rh := common.HexToHash("0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
	wh := common.Hash{}
	hdr := &types.Header{
		Number:           big.NewInt(100),
		Time:             1763568002,
		WithdrawalsHash:  &wh,
		RequestsHash:     &rh,
		ParentBeaconRoot: &rh,
	}
	exp := liveExpectedHeader(hdr)
	if exp.RequestsHash == nil || *exp.RequestsHash != rh.Hex() {
		t.Fatalf("requestsHash not emitted: %v", exp.RequestsHash)
	}
	// Pre-requests header: field must stay omitted (absent-vs-absent passes).
	hdr.RequestsHash = nil
	exp = liveExpectedHeader(hdr)
	if exp.RequestsHash != nil {
		t.Fatalf("requestsHash emitted for nil header field: %v", *exp.RequestsHash)
	}
}

func TestParseDumpBalance(t *testing.T) {
	cases := []struct {
		in   string
		want string // decimal
		err  bool
	}{
		{"0", "0", false},                                  // empty-account balance
		{"1000000000000000000", "1000000000000000000", false}, // 1 ETH decimal (op-geth format)
		{"0xde0b6b3a7640000", "1000000000000000000", false}, // 0x-hex fallback
		{"garbage", "", true},
		{"", "", true},
	}
	for _, c := range cases {
		got, err := parseDumpBalance(c.in)
		if c.err {
			if err == nil {
				t.Fatalf("parseDumpBalance(%q): expected error, got %v", c.in, got)
			}
			continue
		}
		if err != nil || got.String() != c.want {
			t.Fatalf("parseDumpBalance(%q) = %v, %v; want %s", c.in, got, err, c.want)
		}
	}
}

func TestLiveChainConfig(t *testing.T) {
	// Minimal registry-shaped config; the mapping must set the ETH twins
	// (CheckOptimismValidity requires canyon==shanghai, ecotone==cancun,
	// isthmus==prague) and the OP fork times verbatim.
	jovian := uint64(1763568001)
	isthmus := uint64(1744905600)
	sc := &superchain.ChainConfig{
		ChainID: liveChainID,
		Hardforks: superchain.HardforkConfig{
			CanyonTime:  uint64Ptr(1699981200),
			EcotoneTime: uint64Ptr(1708534800),
			FjordTime:   uint64Ptr(1716998400),
			GraniteTime: uint64Ptr(1723478400),
			HoloceneTime: uint64Ptr(1732633200),
			IsthmusTime: &isthmus,
			JovianTime:  &jovian,
		},
		Optimism: &superchain.OptimismConfig{
			EIP1559Elasticity:        6,
			EIP1559Denominator:       50,
			EIP1559DenominatorCanyon: uint64Ptr(250),
		},
	}
	cfg := liveChainConfig(sc)
	if cfg.ChainID.Uint64() != liveChainID {
		t.Fatalf("chain id %d != %d", cfg.ChainID.Uint64(), liveChainID)
	}
	if cfg.CanyonTime == nil || *cfg.CanyonTime != 1699981200 ||
		cfg.ShanghaiTime == nil || *cfg.ShanghaiTime != 1699981200 {
		t.Fatalf("canyon/shanghai twin broken: %v/%v", cfg.CanyonTime, cfg.ShanghaiTime)
	}
	if cfg.IsthmusTime == nil || *cfg.IsthmusTime != isthmus ||
		cfg.PragueTime == nil || *cfg.PragueTime != isthmus {
		t.Fatalf("isthmus/prague twin broken: %v/%v", cfg.IsthmusTime, cfg.PragueTime)
	}
	if cfg.JovianTime == nil || *cfg.JovianTime != jovian {
		t.Fatalf("jovian time %v != %d", cfg.JovianTime, jovian)
	}
	if cfg.KarstTime != nil || cfg.InteropTime != nil {
		t.Fatalf("karst/interop must stay nil (pin predates them)")
	}
	if err := cfg.CheckOptimismValidity(); err != nil {
		t.Fatalf("CheckOptimismValidity: %v", err)
	}
	if !cfg.IsJovian(jovian) || !cfg.IsIsthmus(isthmus) {
		t.Fatalf("fork predicates wrong: IsJovian(%d)=%v IsIsthmus(%d)=%v",
			jovian, cfg.IsJovian(jovian), isthmus, cfg.IsIsthmus(isthmus))
	}
}

// Guard: liveChainConfig must never include Karst (the pinned registry predates
// it; the window check enforces the bound) — kept here as a named assertion.
func TestLiveChainConfigNoKarst(t *testing.T) {
	sc := &superchain.ChainConfig{ChainID: liveChainID, Hardforks: superchain.HardforkConfig{}}
	cfg := liveChainConfig(sc)
	if cfg.KarstTime != nil {
		t.Fatalf("KarstTime must be nil, got %v", *cfg.KarstTime)
	}
	if !strings.Contains(cfg.ChainID.String(), "11155420") {
		t.Fatalf("unexpected chain id %s", cfg.ChainID)
	}
	_ = params.ChainConfig{}
}
