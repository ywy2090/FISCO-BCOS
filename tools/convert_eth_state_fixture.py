#!/usr/bin/env python3
"""
Skeleton converter: simplified intermediate JSON -> bcos-evm fixture schema.

Expected intermediate shape (example):
{
  "name": "stExample_return42",
  "source": "ethereum/tests/GeneralStateTests/stExample/...",
  "revision": "prague",
  "tx": { "from": "0x...", "to": "0x...", "gas_limit": 100000, ... },
  "block": { "number": 1, ... },
  "pre": [ { "address": "0x...", "balance": "0x0", "nonce": 0, "code": "0x" } ],
  "expected": { "status": "EVMC_SUCCESS", "gas_used": 0, "logs": 0, "output": "0x" }
}

Usage:
  python3 tools/convert_eth_state_fixture.py input.json -o bcos-evm/test/fixtures/state/imported/out.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


REQUIRED_TOP_LEVEL = ("name", "source", "revision", "tx", "block", "expected")
REQUIRED_TX = ("from", "gas_limit")
REQUIRED_BLOCK = ("number", "timestamp", "gas_limit", "coinbase", "base_fee", "chain_id")
REQUIRED_EXPECTED = ("status",)


def validate_intermediate(doc: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for key in REQUIRED_TOP_LEVEL:
        if key not in doc:
            errors.append(f"missing top-level field: {key}")
    if "tx" in doc and isinstance(doc["tx"], dict):
        for key in REQUIRED_TX:
            if key not in doc["tx"]:
                errors.append(f"missing tx.{key}")
    if "block" in doc and isinstance(doc["block"], dict):
        for key in REQUIRED_BLOCK:
            if key not in doc["block"]:
                errors.append(f"missing block.{key}")
    if "expected" in doc and isinstance(doc["expected"], dict):
        for key in REQUIRED_EXPECTED:
            if key not in doc["expected"]:
                errors.append(f"missing expected.{key}")
    if not doc.get("source"):
        errors.append("source must be non-empty")
    return errors


def convert(doc: dict[str, Any]) -> dict[str, Any]:
    """Pass-through with defaults; extend here for upstream GeneralStateTests formats."""
    out = {
        "name": doc["name"],
        "source": doc["source"],
        "revision": doc.get("revision", "prague"),
        "tx": {
            "from": doc["tx"]["from"],
            "gas_limit": doc["tx"]["gas_limit"],
            "gas_price": doc["tx"].get("gas_price", "0x0"),
            "value": doc["tx"].get("value", "0x0"),
            "nonce": doc["tx"].get("nonce", 0),
            "data": doc["tx"].get("data", "0x"),
        },
        "block": doc["block"],
        "pre": doc.get("pre", []),
        "expected": {
            "status": doc["expected"]["status"],
            "gas_used": doc["expected"].get("gas_used", 0),
            "logs": doc["expected"].get("logs", 0),
            "output": doc["expected"].get("output", "0x"),
        },
    }
    if "to" in doc["tx"]:
        out["tx"]["to"] = doc["tx"]["to"]
    if "tx_props" in doc:
        out["tx_props"] = doc["tx_props"]
    if "gas_used_tolerance" in doc["expected"]:
        out["expected"]["gas_used_tolerance"] = doc["expected"]["gas_used_tolerance"]
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="intermediate JSON file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output fixture path (default: stdout)",
    )
    args = parser.parse_args(argv)

    doc = json.loads(args.input.read_text(encoding="utf-8"))
    errors = validate_intermediate(doc)
    if errors:
        for err in errors:
            print(f"error: {err}", file=sys.stderr)
        return 1

    fixture = convert(doc)
    payload = json.dumps(fixture, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8")
    else:
        sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
