#!/usr/bin/env python3
"""Minimal manifest linter for specs-tests."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def lint_manifest(path: Path, capability_rows: set[str]) -> list[str]:
    errors: list[str] = []
    data = load_json(path)
    entries = data.get("entries", [])
    seen_ids: set[str] = set()

    for index, entry in enumerate(entries):
        evidence_id = entry.get("evidenceId")
        if not evidence_id:
            errors.append(f"{path}: entries[{index}] missing evidenceId")
            continue
        if evidence_id in seen_ids:
            errors.append(f"{path}: duplicate evidenceId {evidence_id}")
        seen_ids.add(evidence_id)

        for row_id in entry.get("capabilityRowIds", []):
            if row_id not in capability_rows:
                errors.append(f"{path}: unknown capabilityRowId {row_id} in {evidence_id}")

        if entry.get("evidenceKind") == "ReferenceParity" and entry.get("path") != "Reference":
            errors.append(f"{path}: ReferenceParity requires path=Reference for {evidence_id}")

    return errors


def lint_expectations(path: Path) -> list[str]:
    errors: list[str] = []
    data = load_json(path)
    for index, entry in enumerate(data.get("expectations", [])):
        for field in ("reasonClass", "issueOrAdr", "reviewBy"):
            if field not in entry or not entry[field]:
                errors.append(f"{path}: expectations[{index}] missing {field}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifests", type=Path, required=True)
    parser.add_argument("--capability-rows", type=Path, required=True)
    args = parser.parse_args()

    capability_rows = set(load_json(args.capability_rows).get("rows", []))
    errors: list[str] = []

    for manifest in sorted(args.manifests.rglob("*.json")):
        if manifest.name in {"schema.json", "capability-rows.json", "expectations.json",
                             "opstack-skip-list.json"}:
            continue
        errors.extend(lint_manifest(manifest, capability_rows))

    expectations_path = args.manifests / "expectations.json"
    if expectations_path.exists():
        errors.extend(lint_expectations(expectations_path))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("manifest lint ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
