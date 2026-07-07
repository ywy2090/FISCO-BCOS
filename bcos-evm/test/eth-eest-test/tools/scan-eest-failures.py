#!/usr/bin/env python3
"""Scan EEST state_tests via EthEestStateGranular; emit failure + bucket reports."""
from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

_TOOLS = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location(
    "bucket_failures", _TOOLS / "bucket-failures.py"
)
assert _spec and _spec.loader
_bucket = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_bucket)
write_bucket_reports = _bucket.write_bucket_reports

DEFAULT_BUILD_DIR = "build-bcos-evm-check"
MANIFEST_REL = "bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json"
REPORTS_REL = "bcos-evm/test/eth-eest-test/reports"


@dataclass
class Failure:
    directory: str
    json_file: str
    subtest: str
    message: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def load_manifest_dirs(manifest_path: Path) -> list[tuple[str, str]]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    out: list[tuple[str, str]] = []
    for entry in data["entries"]:
        rel = entry["casePath"].removeprefix("fixtures/state_tests/")
        out.append((rel, entry["forkProfileId"]))
    return out


def profile_for_dir(manifest_dirs: list[tuple[str, str]], rel: str) -> str | None:
    for directory, profile in manifest_dirs:
        if directory == rel:
            return profile
    return None


def parse_args(manifest_dirs: list[tuple[str, str]]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan EEST granular runner failures and bucket by assertion kind"
    )
    parser.add_argument(
        "--build-dir",
        default=DEFAULT_BUILD_DIR,
        help=f"CMake build tree (default: {DEFAULT_BUILD_DIR})",
    )
    parser.add_argument(
        "--dir",
        metavar="REL",
        help="Single directory under fixtures/state_tests/ (e.g. cancun/eip4844_blobs)",
    )
    parser.add_argument(
        "--profile",
        metavar="ID",
        help="Override fork profile (default: manifest entry for --dir)",
    )
    parser.add_argument(
        "--manifest-16",
        action="store_true",
        help="Scan all manifest dirs from eth-eest-state-full.json (default)",
    )
    parser.add_argument(
        "--granular-full",
        action="store_true",
        help="One full-tree EthEestStateGranular run + XML parse (nightly artifact)",
    )
    args = parser.parse_args()

    if args.dir and args.granular_full:
        parser.error("--dir and --granular-full are mutually exclusive")

    if not args.dir and not args.granular_full:
        args.manifest_16 = True

    if args.dir and not args.profile:
        args.profile = profile_for_dir(manifest_dirs, args.dir)
        if not args.profile:
            parser.error(f"--profile required: {args.dir!r} not in eth-eest-state-full.json")

    return args


def run_file(
    granular: Path,
    json_path: Path,
    eest_root: Path,
    profile: str,
    xml_out: Path,
) -> list[Failure]:
    rel_dir = str(json_path.parent.resolve().relative_to(eest_root))
    cmd = [
        str(granular.resolve()),
        str(json_path),
        "--fork-profiles",
        profile,
        f"--gtest_output=xml:{xml_out}",
    ]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=180, check=False)
    except subprocess.TimeoutExpired:
        return [
            Failure(rel_dir, json_path.name, "(runner error)", "timeout after 180s")
        ]

    if not xml_out.exists():
        return [
            Failure(rel_dir, json_path.name, "(runner error)", "no gtest xml produced")
        ]

    return parse_subtest_xml(xml_out, rel_dir, json_path.name)


def parse_subtest_xml(xml_path: Path, rel_dir: str, json_file: str) -> list[Failure]:
    tree = ET.parse(xml_path)
    root = tree.getroot()
    fails: list[Failure] = []
    for case in root.iter("testcase"):
        for fail in case.findall("failure"):
            name = case.get("name", "")
            classname = case.get("classname", "")
            subtest = f"{classname}/{name}" if classname else name
            msg = (fail.text or fail.get("message") or "").strip().split("\n")[0][:200]
            fails.append(Failure(rel_dir, json_file, subtest, msg))
    return fails


def parse_granular_full_results(
    xml_path: Path,
) -> tuple[list[Failure], dict[str, dict[str, int]]]:
    """Parse file-level GTest XML from EthEestStateGranular full-tree run."""
    tree = ET.parse(xml_path)
    root = tree.getroot()
    fails: list[Failure] = []
    file_failed: dict[str, dict[str, bool]] = {}

    for case in root.iter("testcase"):
        rel_dir = case.get("classname", "") or "(unknown)"
        json_stem = case.get("name", "")
        json_file = f"{json_stem}.json" if json_stem else "(unknown)"
        failed = case.find("failure") is not None
        file_failed.setdefault(rel_dir, {})[json_stem or json_file] = failed

        if not failed:
            continue
        for fail in case.findall("failure"):
            subtest = f"{rel_dir}/{json_stem}" if json_stem else rel_dir
            msg = (fail.text or fail.get("message") or "").strip().split("\n")[0][:200]
            fails.append(Failure(rel_dir, json_file, subtest, msg))

    dir_stats: dict[str, dict[str, int]] = {}
    for rel_dir, files in sorted(file_failed.items()):
        json_files = len(files)
        failed_files = sum(1 for failed in files.values() if failed)
        dir_stats[rel_dir] = {
            "json_files": json_files,
            "json_files_all_pass": json_files - failed_files,
            "subtest_failures": failed_files,
        }
    return fails, dir_stats


def run_granular_full(
    granular: Path, eest_root: Path, xml_out: Path
) -> tuple[list[Failure], dict[str, dict[str, int]]]:
    cmd = [
        str(granular.resolve()),
        str(eest_root),
        f"--gtest_output=xml:{xml_out}",
    ]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=14400, check=False)
    except subprocess.TimeoutExpired:
        return (
            [Failure("(full tree)", "(runner error)", "(runner error)", "timeout after 14400s")],
            {},
        )

    if not xml_out.exists():
        return (
            [
                Failure(
                    "(full tree)", "(runner error)", "(runner error)", "no gtest xml produced"
                )
            ],
            {},
        )

    return parse_granular_full_results(xml_out)


def scan_directories(
    dirs: list[tuple[str, str]],
    *,
    granular: Path,
    eest_root: Path,
    xml_dir: Path,
) -> tuple[list[Failure], dict[str, dict[str, int]]]:
    all_failures: list[Failure] = []
    dir_stats: dict[str, dict[str, int]] = {}

    for rel, profile in dirs:
        dpath = eest_root / rel
        if not dpath.is_dir():
            print(f"{rel}: missing directory {dpath}", file=sys.stderr)
            dir_stats[rel] = {
                "json_files": 0,
                "json_files_all_pass": 0,
                "subtest_failures": 0,
            }
            continue

        files = sorted(p for p in dpath.rglob("*.json") if p.name != "index.json")
        dir_fail = 0
        dir_pass_files = 0
        for jf in files:
            xml_out = xml_dir / f"{rel.replace('/', '_')}_{jf.stem}.xml"
            fails = run_file(granular, jf, eest_root, profile, xml_out)
            if fails:
                dir_fail += len(fails)
                all_failures.extend(fails)
            else:
                dir_pass_files += 1
        dir_stats[rel] = {
            "json_files": len(files),
            "json_files_all_pass": dir_pass_files,
            "subtest_failures": dir_fail,
        }
        print(
            f"{rel}: {dir_fail} subtest failures "
            f"({dir_pass_files}/{len(files)} files clean)"
        )

    return all_failures, dir_stats


def write_inventory_reports(
    out_dir: Path,
    *,
    scope: str,
    failures: list[Failure],
    dir_stats: dict[str, dict[str, int]],
    stem: str,
) -> tuple[Path, Path]:
    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    report = {
        "generated": generated,
        "scope": scope,
        "total_subtest_failures": len(failures),
        "by_directory": dir_stats,
        "failures": [asdict(f) for f in failures],
    }
    json_path = out_dir / f"{stem}.json"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    md_lines = [
        f"# EEST Failure Report ({stem})",
        "",
        f"**Total subtest failures:** {len(failures)}",
        "",
        "## By directory",
        "",
        "| Directory | JSON files | Clean files | Subtest failures |",
        "|-----------|------------|-------------|------------------|",
    ]
    for rel, st in dir_stats.items():
        md_lines.append(
            f"| `{rel}` | {st['json_files']} | {st['json_files_all_pass']} | "
            f"{st['subtest_failures']} |"
        )
    md_lines.extend(["", "## Failure inventory", ""])
    current_dir = ""
    for f in failures:
        if f.directory != current_dir:
            current_dir = f.directory
            md_lines.append(f"### `{current_dir}`")
            md_lines.append("")
        md_lines.append(f"- **{f.json_file}** / `{f.subtest}`")
        md_lines.append(f"  - {f.message}")
    md_path = out_dir / f"{stem}.md"
    md_path.write_text("\n".join(md_lines) + "\n", encoding="utf-8")
    return json_path, md_path


def main() -> int:
    root = repo_root()
    manifest_path = root / MANIFEST_REL
    manifest_dirs = load_manifest_dirs(manifest_path)
    args = parse_args(manifest_dirs)

    build = root / args.build_dir
    granular = build / "bcos-evm/test/eth-eest-test/EthEestStateGranular"
    eest = (build / "_deps/evm_ref_eest_root/fixtures/state_tests").resolve()

    if not granular.is_file():
        print(f"Missing {granular}", file=sys.stderr)
        return 1
    if not eest.is_dir():
        print(f"Missing EEST fixtures {eest}", file=sys.stderr)
        return 1

    out_dir = root / REPORTS_REL
    out_dir.mkdir(parents=True, exist_ok=True)
    xml_dir = out_dir / "gtest-xml"
    xml_dir.mkdir(exist_ok=True)

    if args.granular_full:
        scope = "EthEestStateGranular full tree (fixtures/state_tests)"
        xml_out = xml_dir / "granular_full.xml"
        all_failures, dir_stats = run_granular_full(granular, eest, xml_out)
        inventory_stem = "eest-granular-full-failures"
        total_files = sum(st["json_files"] for st in dir_stats.values())
        clean_files = sum(st["json_files_all_pass"] for st in dir_stats.values())
        print(
            f"full tree: {len(all_failures)} file failures "
            f"({clean_files}/{total_files} files clean, {len(dir_stats)} dirs)"
        )
        for rel, st in sorted(dir_stats.items()):
            if st["subtest_failures"]:
                print(
                    f"  {rel}: {st['subtest_failures']} failures "
                    f"({st['json_files_all_pass']}/{st['json_files']} files clean)"
                )
    elif args.dir:
        scope = f"single dir {args.dir} (profile {args.profile})"
        all_failures, dir_stats = scan_directories(
            [(args.dir, args.profile)],
            granular=granular,
            eest_root=eest,
            xml_dir=xml_dir,
        )
        safe = args.dir.replace("/", "_")
        inventory_stem = f"eest-dir-{safe}-failures"
    else:
        scope = "eth-eest-state-full.json (16 manifest dirs)"
        all_failures, dir_stats = scan_directories(
            manifest_dirs,
            granular=granular,
            eest_root=eest,
            xml_dir=xml_dir,
        )
        inventory_stem = "eest-state-full-failures"

    inv_json, inv_md = write_inventory_reports(
        out_dir,
        scope=scope,
        failures=all_failures,
        dir_stats=dir_stats,
        stem=inventory_stem,
    )
    bucket_json, bucket_md = write_bucket_reports(
        out_dir,
        scope=scope,
        failures=[asdict(f) for f in all_failures],
        by_directory=dir_stats,
    )

    print(f"\nWrote {inv_json}")
    print(f"Wrote {inv_md}")
    print(f"Wrote {bucket_json}")
    print(f"Wrote {bucket_md}")
    if all_failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
