#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Offline generator: superchain-configs.zip -> FISCO config.genesis fragment
([eth_genesis_header] + [alloc.N] + [op_fork_schedule]) and op-node rollup.json.

The zip is op-geth's embedded registry (superchain/superchain-configs.zip): a COMMIT
pin, a shared zstd `dictionary`, `configs/<network>/<name>.toml` and a dictionary-
compressed `genesis/<network>/<name>.json.zst`. Everything here is offline: no
network, no op-geth binary, and the only external process is the zstd CLI.
"""
import argparse
import json
import subprocess
import sys
import tempfile
import tomllib
import zipfile
from pathlib import Path


class RegistryError(ValueError):
    """The registry zip is missing something, or contradicts the requested chain."""


def default_decompress(zst_bytes, dictionary):
    """Decompress a `-D dictionary` zstd frame by shelling out to the zstd CLI.

    Frames are dictionary-compressed, so a plain `zstd -d` fails; the dictionary has
    to be written to a file (the CLI has no stdin form for it) and passed with -D.
    """
    with tempfile.NamedTemporaryFile() as dict_file:
        dict_file.write(dictionary)
        dict_file.flush()
        proc = subprocess.run(
            ["zstd", "-d", "-D", dict_file.name, "-c"],
            input=zst_bytes, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError("zstd decompression failed: " + proc.stderr.decode(errors="replace"))
    return proc.stdout


def load_registry_chain(zip_path, chain, *, decompress=None):
    """Read one chain out of the registry zip.

    `chain` is `<network>/<name>`, e.g. `mainnet/base`. `decompress` is injectable so
    the tests can feed a plaintext fixture without the zstd CLI.
    """
    if not Path(zip_path).is_file():
        raise RegistryError(f"registry zip not found: {zip_path}")
    with zipfile.ZipFile(zip_path) as zf:
        names = set(zf.namelist())
        toml_name = f"configs/{chain}.toml"
        genesis_name = f"genesis/{chain}.json.zst"
        for required in (toml_name, genesis_name, "dictionary", "COMMIT"):
            if required not in names:
                raise RegistryError(f"registry zip missing entry: {required}")
        commit = zf.read("COMMIT").decode().strip()
        toml = tomllib.loads(zf.read(toml_name).decode())
        dictionary = zf.read("dictionary")
        zst = zf.read(genesis_name)
    dec = decompress or default_decompress
    genesis = json.loads(dec(zst, dictionary))
    return {"commit": commit, "chain": chain, "toml": toml, "genesis": genesis}


import importlib.util  # noqa: E402  (appended section; see _load below)

_DIR = Path(__file__).parent


def _load(mod_name, filename):
    """Load a sibling script as a module (they are not importable by name: the
    directory is not a package and `build-allocs.py` is not an identifier)."""
    spec = importlib.util.spec_from_file_location(mod_name, str(_DIR / filename))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_fixture = _load("gen_eth_header_fixture", "gen_eth_header_fixture.py")
keccak256 = _fixture.keccak256
HEADER_FIELD_ORDER = _fixture.HEADER_FIELD_ORDER

EMPTY_TRIE_ROOT = "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
EMPTY_OMMERS_HASH = "1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"
EMPTY_REQUESTS_HASH = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

# The EL forks, in protocol order. `delta` is deliberately absent: it has no EL
# semantics (op-geth params/config_op.go has no DeltaTime field).
EL_FORKS = ["regolith", "canyon", "ecotone", "fjord", "granite",
            "holocene", "isthmus", "jovian", "karst"]
# A pre-Canyon genesis header is exactly the London field set: the registry JSON is a
# superset (Base's 2023 genesis carries blobGasUsed/excessBlobGas), so the field set is
# chosen by genesis TIME, never by which keys the JSON happens to have.
LONDON_FIELDS = ["parent_hash", "sha3_uncles", "miner", "state_root",
                 "transactions_root", "receipts_root", "logs_bloom", "difficulty",
                 "number", "gas_limit", "gas_used", "timestamp", "extra_data",
                 "mix_hash", "nonce", "base_fee_per_gas"]


def header_field_set(ts0, fork_times):
    """Ordered present-keys: London always; fork-gated higher fields by genesis time.

    Withdrawals arrive at Canyon (Shanghai), the blob pair + beacon root at Ecotone
    (Cancun), requests_hash at Isthmus (Prague). A fork missing from `fork_times` is
    not scheduled, so its fields never appear.
    """
    fields = list(LONDON_FIELDS)
    inf = float("inf")
    if ts0 >= fork_times.get("canyon", inf):
        fields.append("withdrawals_root")
    if ts0 >= fork_times.get("ecotone", inf):
        fields += ["blob_gas_used", "excess_blob_gas", "parent_beacon_block_root"]
    if ts0 >= fork_times.get("isthmus", inf):
        fields.append("requests_hash")
    return fields


def _hex_default(genesis, key, default):
    value = genesis.get(key)
    return value if value is not None else default


def build_header_fields(genesis, ts0, state_root, present):
    """Map registry genesis JSON fields onto the FISCO header keys in `present`.

    Only the keys in `present` are produced: the JSON is a superset input, so anything
    the genesis fork cannot carry must not leak into the RLP.
    """
    defaults = {
        "parent_hash": "0x" + "00" * 32, "sha3_uncles": "0x" + EMPTY_OMMERS_HASH,
        "miner": "0x" + "00" * 20, "transactions_root": "0x" + EMPTY_TRIE_ROOT,
        "receipts_root": "0x" + EMPTY_TRIE_ROOT, "logs_bloom": "0x" + "00" * 256,
        "difficulty": "0x0", "number": "0x0", "gas_limit": "0x0", "gas_used": "0x0",
        "timestamp": "0x0", "extra_data": "0x", "mix_hash": "0x" + "00" * 32,
        "nonce": "0x0000000000000000", "base_fee_per_gas": "0x0",
        "withdrawals_root": "0x" + EMPTY_TRIE_ROOT, "blob_gas_used": "0x0",
        "excess_blob_gas": "0x0", "parent_beacon_block_root": "0x" + "00" * 32,
        "requests_hash": "0x" + EMPTY_REQUESTS_HASH,
    }
    # (registry key, fixed value): state_root is computed from the allocs, not read.
    mapping = {
        "parent_hash": ("parentHash", None), "sha3_uncles": ("sha3Uncles", None),
        "miner": ("coinbase", None), "state_root": (None, state_root),
        "transactions_root": ("transactionsRoot", None),
        "receipts_root": ("receiptsRoot", None),
        "logs_bloom": ("logsBloom", None), "difficulty": ("difficulty", None),
        "number": ("number", None), "gas_limit": ("gasLimit", None),
        "gas_used": ("gasUsed", None), "timestamp": ("timestamp", None),
        "extra_data": ("extraData", None), "mix_hash": ("mixHash", None),
        "nonce": ("nonce", None), "base_fee_per_gas": ("baseFeePerGas", None),
        "withdrawals_root": ("withdrawalsRoot", None), "blob_gas_used": ("blobGasUsed", None),
        "excess_blob_gas": ("excessBlobGas", None),
        "parent_beacon_block_root": ("parentBeaconBlockRoot", None),
        "requests_hash": ("requestsHash", None),
    }
    out = {}
    for key in present:
        src, fixed = mapping[key]
        if fixed is not None:
            text = fixed.hex() if isinstance(fixed, (bytes, bytearray)) else fixed
            out[key] = "0x" + text
        else:
            out[key] = _hex_default(genesis, src, defaults[key])
    # The registry `nonce` is a quantity ("0x0"), while the header field is exactly 8
    # bytes and the INI wants 16 hex digits: normalize, never bytes.fromhex directly.
    if "nonce" in out:
        out["nonce"] = "0x" + int(out["nonce"], 16).to_bytes(8, "big").hex()
    return out


def encode_header_fields(fields, present):
    return _fixture.encode_header(fields, present)


_trieroot = _load("gen_trieroot_golden", "gen_trieroot_golden.py")
_build_allocs = _load("build_allocs", "build-allocs.py")


def _strip0x(value):
    text = str(value).strip().lower()
    return text[2:] if text.startswith("0x") else text


def _to_int(value):
    """Registry quantities are hex strings ("0x1c9c380"); INI wants decimal ints.

    `int(x)` on such a string would either raise or silently read it as decimal, so
    every quantity goes through here.
    """
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if text in ("", "0x", "0X"):
        return 0
    return int(text, 16) if text.lower().startswith("0x") else int(text)


def _word32(value):
    return _to_int(value).to_bytes(32, "big")


def compute_state_root(alloc):
    """State root over the registry's alloc (address -> account), go-ethereum's
    Genesis.ToBlock construction: see gen_trieroot_golden.state_root."""
    tuples = []
    for address, account in alloc.items():
        storage = {_word32(slot): _word32(value)
                   for slot, value in (account.get("storage") or {}).items()}
        code_hex = _strip0x(account.get("code", ""))
        code = bytes.fromhex(code_hex) if code_hex else b""
        tuples.append((bytes.fromhex(_strip0x(address)),
                       _to_int(account.get("nonce", 0)),
                       _to_int(account.get("balance", 0)),
                       code, storage))
    return _trieroot.state_root(tuples)


def to_ini_allocs(alloc):
    """Shape the registry alloc as build-allocs.emit_ini expects: address lowercased,
    balance/nonce decimal, storage slot/value as integers."""
    out = []
    for address, account in alloc.items():
        out.append({
            "address": _strip0x(address),
            "balance": _to_int(account.get("balance", 0)),
            "nonce": _to_int(account.get("nonce", 0)),
            "code": account.get("code", ""),
            "storage": {_to_int(slot): _to_int(value)
                        for slot, value in (account.get("storage") or {}).items()},
        })
    return out


# RegistryError is defined above (Task 2): this section reuses it rather than
# re-declaring the class.


def _fork_times(toml, extra_forks):
    """EL fork name -> activation time, from the pin plus any `--extra-fork` overlay.

    `delta` never appears: it is not an EL fork. An overlay may only add forks the pin
    lacks (karst) or repeat a pinned time exactly; a conflicting time is an error
    rather than a silent override.
    """
    hardforks = dict(toml.get("hardforks", {}))
    for name, timestamp in (extra_forks or {}).items():
        if name not in EL_FORKS:
            raise RegistryError("unknown EL fork in --extra-fork: " + name)
        if name == "regolith":
            raise RegistryError("regolith baseline is implicit; do not overlay it")
        existing = hardforks.get(f"{name}_time")
        if existing is not None and int(existing) != int(timestamp):
            raise RegistryError(
                f"overlay {name}:{timestamp} conflicts with pin {name}_time={existing}")
        hardforks[f"{name}_time"] = timestamp
    times = {"regolith": 0}
    for fork in EL_FORKS[1:]:
        value = hardforks.get(f"{fork}_time")
        if value is not None:
            times[fork] = int(value)
    return times


def build_schedule(toml, ts0, extra_forks=None):
    """Canonical `[op_fork_schedule]` string for this chain.

    The baseline is the EL fork active at genesis, written as `0:<fork>` (the S2 codec
    requires a timestamp-0 baseline); only forks after it appear, and they must be
    contiguous — the codec rejects a skipped fork, so a gap here is a hard error.
    """
    times = _fork_times(toml, extra_forks)
    present = [f for f in EL_FORKS if f in times]
    indices = [EL_FORKS.index(f) for f in present]
    if indices != list(range(indices[0], indices[0] + len(indices))):
        raise RegistryError("non-contiguous fork schedule: " + ",".join(present))
    for earlier, later in zip(present, present[1:]):
        if times[later] < times[earlier]:
            raise RegistryError(f"fork time regresses: {earlier}->{later}")
    baseline = EL_FORKS[0]
    for fork in present:
        if times[fork] <= ts0:
            baseline = fork
        else:
            break
    parts = [f"0:{baseline}"]
    for fork in EL_FORKS[EL_FORKS.index(baseline) + 1:]:
        if fork not in times:
            break
        parts.append(f"{times[fork]}:{fork}")
    return ",".join(parts)
