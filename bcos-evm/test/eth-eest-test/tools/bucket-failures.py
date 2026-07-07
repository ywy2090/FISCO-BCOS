#!/usr/bin/env python3
"""Classify EEST granular failures into assertion-kind buckets (JSON + MD)."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

# Ordered taxonomy: first match wins.
BUCKET_PATTERNS: list[tuple[str, list[str]]] = [
    ("state_root", ["stateRoot mismatch", "state root"]),
    ("logs_hash", ["logsHash", "logs hash"]),
    ("gas_used", ["gasUsed", "gas used", "intrinsic gas"]),
    ("expect_exception", ["expectException", "expected reverted"]),
    ("balance_nonce", ["balance", "nonce", "coinbase"]),
    ("code_storage", ["code mismatch", "storage"]),
    ("runner_error", ["no gtest xml", "timeout", "SEGV"]),
]


def classify_failure(message: str) -> str:
    """Return bucket id for a failure message (case-insensitive substring match)."""
    haystack = message.lower()
    for bucket, patterns in BUCKET_PATTERNS:
        for pattern in patterns:
            if pattern.lower() in haystack:
                return bucket
    return "unknown"


def _slug_timestamp(when: datetime | None = None) -> str:
    ts = when or datetime.now(timezone.utc)
    return ts.strftime("%Y%m%dT%H%M%SZ")


def bucketize_failures(failures: list[dict[str, Any]]) -> dict[str, Any]:
    """Add ``bucket`` to each failure and build per-bucket counts + inventory."""
    bucketed: list[dict[str, Any]] = []
    counts: dict[str, int] = {name: 0 for name, _ in BUCKET_PATTERNS}
    counts["unknown"] = 0
    by_bucket: dict[str, list[dict[str, Any]]] = {k: [] for k in counts}

    for item in failures:
        entry = dict(item)
        bucket = classify_failure(str(entry.get("message", "")))
        entry["bucket"] = bucket
        bucketed.append(entry)
        counts[bucket] = counts.get(bucket, 0) + 1
        by_bucket.setdefault(bucket, []).append(entry)

    return {
        "failures": bucketed,
        "by_bucket": counts,
        "buckets": by_bucket,
    }


def write_bucket_reports(
    out_dir: Path,
    *,
    scope: str,
    failures: list[dict[str, Any]],
    by_directory: dict[str, dict[str, int]] | None = None,
    timestamp: str | None = None,
) -> tuple[Path, Path]:
    """Write ``eest-granular-failures-<timestamp>.{json,md}`` and return paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    ts = timestamp or _slug_timestamp()
    grouped = bucketize_failures(failures)

    report: dict[str, Any] = {
        "generated": ts,
        "scope": scope,
        "total_subtest_failures": len(failures),
        "by_bucket": grouped["by_bucket"],
        "by_directory": by_directory or {},
        "failures": grouped["failures"],
    }

    json_path = out_dir / f"eest-granular-failures-{ts}.json"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    md_lines = [
        "# EEST Granular Failure Bucket Report",
        "",
        f"**Generated:** {ts}",
        f"**Scope:** {scope}",
        f"**Total subtest failures:** {len(failures)}",
        "",
        "## By bucket",
        "",
        "| Bucket | Count |",
        "|--------|-------|",
    ]
    for bucket, _ in BUCKET_PATTERNS:
        md_lines.append(f"| `{bucket}` | {grouped['by_bucket'].get(bucket, 0)} |")
    md_lines.append(f"| `unknown` | {grouped['by_bucket'].get('unknown', 0)} |")

    if by_directory:
        md_lines.extend(
            [
                "",
                "## By directory",
                "",
                "| Directory | JSON files | Clean files | Subtest failures |",
                "|-----------|------------|-------------|------------------|",
            ]
        )
        for rel, st in by_directory.items():
            md_lines.append(
                f"| `{rel}` | {st['json_files']} | {st['json_files_all_pass']} | "
                f"{st['subtest_failures']} |"
            )

    md_lines.extend(["", "## Failure inventory by bucket", ""])
    bucket_order = [b for b, _ in BUCKET_PATTERNS] + ["unknown"]
    for bucket in bucket_order:
        items = grouped["buckets"].get(bucket, [])
        if not items:
            continue
        md_lines.append(f"### `{bucket}` ({len(items)})")
        md_lines.append("")
        current_dir = ""
        for f in items:
            directory = str(f.get("directory", ""))
            if directory != current_dir:
                current_dir = directory
                md_lines.append(f"#### `{current_dir}`")
                md_lines.append("")
            md_lines.append(f"- **{f.get('json_file', '')}** / `{f.get('subtest', '')}`")
            md_lines.append(f"  - {f.get('message', '')}")

    md_path = out_dir / f"eest-granular-failures-{ts}.md"
    md_path.write_text("\n".join(md_lines) + "\n", encoding="utf-8")
    return json_path, md_path


def main() -> int:
    """CLI: bucket an existing failures JSON (``failures`` array required)."""
    import argparse
    import sys

    parser = argparse.ArgumentParser(description="Bucket EEST granular failure messages")
    parser.add_argument("input", type=Path, help="JSON with top-level ``failures`` list")
    parser.add_argument(
        "-o",
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory (default: alongside input)",
    )
    args = parser.parse_args()

    data = json.loads(args.input.read_text(encoding="utf-8"))
    failures = data.get("failures", [])
    scope = data.get("scope", args.input.stem)
    by_directory = data.get("by_directory")
    out_dir = args.out_dir or args.input.parent

    json_path, md_path = write_bucket_reports(
        out_dir,
        scope=scope,
        failures=failures,
        by_directory=by_directory,
    )
    print(f"Wrote {json_path}")
    print(f"Wrote {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
