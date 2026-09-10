# Copyright (c) FISCO-BCOS, Apache-2.0
import importlib.util
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "gen_eth_header_fixture", str(Path(__file__).parent / "gen_eth_header_fixture.py"))
f = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(f)

EMPTY_TRIE = "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
OMMERS = "1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"


def test_default_21_fields_unchanged():
    digest = f.keccak256(f.encode_header(f.DEFAULT_FIELDS)).hex()
    assert digest == "8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069"


def test_london_16_fields_subset():
    fields = {
        "parent_hash": "0x" + "00" * 32,
        "sha3_uncles": "0x" + OMMERS,
        "miner": "0x4200000000000000000000000000000000000011",
        "state_root": "0x" + EMPTY_TRIE,
        "transactions_root": "0x" + EMPTY_TRIE,
        "receipts_root": "0x" + EMPTY_TRIE,
        "logs_bloom": "0x" + "00" * 256,
        "difficulty": "0x0",
        "number": "0x0",
        "gas_limit": "0x1c9c380",
        "gas_used": "0x0",
        "timestamp": "0x648a5ce3",
        "extra_data": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
        "mix_hash": "0x" + "00" * 32,
        "nonce": "0x0000000000000000",
        "base_fee_per_gas": "0x3b9aca00",
    }
    present = list(fields)  # exactly 16, dict insertion order = go-ethereum London order
    digest = f.keccak256(f.encode_header(fields, present)).hex()
    assert digest == "d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9"
