# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for gen_official_genesis.py. Pure logic uses synthetic fixtures;
the real-zip acceptance test skips when the op-geth zip/zstd are unavailable."""
import importlib.util
import json
import zipfile
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "gen_official_genesis", str(Path(__file__).parent / "gen_official_genesis.py"))
gen = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(gen)

TOML = """
name = "Base"
chain_id = 8453
batch_inbox_addr = "0xFF00000000000000000000000000000000000010"
block_time = 2
seq_window_size = 3600
max_sequencer_drift = 600
[hardforks]
canyon_time = 1704992401
delta_time = 1708560000
ecotone_time = 1710374401
[optimism]
eip1559_elasticity = 6
eip1559_denominator = 50
eip1559_denominator_canyon = 250
[genesis]
l2_time = 1686789347
[genesis.l1]
hash = "0x1111111111111111111111111111111111111111111111111111111111111111"
number = 17481768
[genesis.l2]
hash = "0xd043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"
number = 0
[genesis.system_config]
batcherAddress = "0x5050F69a9786F081509234F1a7F4684b5E5b76C9"
overhead = "0x00000000000000000000000000000000000000000000000000000000000000bc"
scalar = "0x00000000000000000000000000000000000000000000000000000000000a6fe0"
gasLimit = 30000000
[addresses]
OptimismPortalProxy = "0xbEb5Fc579115071764c7423A4f12eDde41f106Ed"
SystemConfigProxy = "0x229047fed2591dbec1eF1118d64F7aF3dB9EB290"
"""

# Field-for-field the Task 1 London 16-field vector (ts 0x648a5ce3, non-empty
# extraData); its keccak256(rlp) equals the l2 hash in TOML above, so the synthetic
# end-to-end run necessarily satisfies self-verification.
GENESIS = {
    "number": "0x0", "timestamp": "0x648a5ce3", "gasLimit": "0x1c9c380",
    "gasUsed": "0x0", "difficulty": "0x0", "baseFeePerGas": "0x3b9aca00",
    "coinbase": "0x4200000000000000000000000000000000000011",
    "parentHash": "0x" + "00" * 32, "mixHash": "0x" + "00" * 32,
    "nonce": "0x0000000000000000",
    "extraData": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
    "alloc": {},
}


def _make_zip(tmp_path):
    path = tmp_path / "superchain-configs.zip"
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("COMMIT", "deadbeef")
        zf.writestr("dictionary", b"")  # unused by the identity decompressor, still read
        zf.writestr("configs/mainnet/base.toml", TOML)
        zf.writestr("genesis/mainnet/base.json.zst", json.dumps(GENESIS))
    return str(path)


def test_load_registry_chain_reads_toml_and_json(tmp_path):
    data = gen.load_registry_chain(_make_zip(tmp_path), "mainnet/base",
                                   decompress=lambda raw, dictionary: raw)
    assert data["commit"] == "deadbeef"
    assert data["toml"]["chain_id"] == 8453
    assert data["toml"]["hardforks"]["canyon_time"] == 1704992401
    assert data["genesis"]["timestamp"] == "0x648a5ce3"


def test_header_field_set_by_genesis_time():
    forks = {"canyon": 100, "ecotone": 200, "isthmus": 400}
    assert gen.header_field_set(0, forks) == gen.LONDON_FIELDS
    assert "withdrawals_root" in gen.header_field_set(100, forks)
    assert "blob_gas_used" in gen.header_field_set(200, forks)
    assert "requests_hash" in gen.header_field_set(400, forks)
    assert "requests_hash" not in gen.header_field_set(399, forks)


def test_build_and_selfcheck_london_header():
    genesis = {
        "number": "0x0", "timestamp": "0x648a5ce3", "gasLimit": "0x1c9c380",
        "gasUsed": "0x0", "difficulty": "0x0", "baseFeePerGas": "0x3b9aca00",
        "mixHash": "0x" + "00" * 32, "nonce": "0x0000000000000000",
        "coinbase": "0x4200000000000000000000000000000000000011",
        "extraData": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
        "parentHash": "0x" + "00" * 32,
    }
    present = gen.header_field_set(0x648a5ce3, {})
    fields = gen.build_header_fields(genesis, 0x648a5ce3, gen.EMPTY_TRIE_ROOT, present)
    digest = gen.keccak256(gen.encode_header_fields(fields, present)).hex()
    assert digest == "d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"
