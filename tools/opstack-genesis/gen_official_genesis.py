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
