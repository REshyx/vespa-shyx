#!/usr/bin/env python3
"""Remap ENTITY_IDs on already-exported PV Exodus + Nodeset files.

Matches vtkSHYXPartitionedCollectionBoundaryAssignment::ReassignEntityIdsBySortedRank:
after area-descending classification (wall, then inlet, then outlets), rewrite side/node
Exodus IDs from the existing ID pools (smallest IDs on the largest patch).

Only PV_*.exo ss_prop1/ns_prop1 and the Nodeset_PV_* "nodeset:" summary line are changed.
Adapter data rows, options, pvsm, and HV files are left alone.

Default is --dry-run. Pass --apply to write. In-place apply copies originals into
<dir>/original_before_entity_id_remap (or --backup-dir) using the original filenames.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

from netCDF4 import Dataset

NODESET_LINE_RE = re.compile(
    r"^(nodeset:\s*)"
    r"inlet\s+(.*?)\s+"
    r"wall\s+(.*?)\s+"
    r"outlet\s*(.*?)\s*$",
    re.IGNORECASE,
)
INTS_RE = re.compile(r"-?\d+")


def parse_ints(text: str) -> list[int]:
    return [int(m.group(0)) for m in INTS_RE.finditer(text)]


@dataclass
class NodesetFile:
    path: Path
    lines: list[str]
    summary_index: int
    inlet: list[int]
    wall: list[int]
    outlet: list[int]

    @property
    def area_rank_ss(self) -> list[int]:
        return list(self.wall) + list(self.inlet) + list(self.outlet)

    @property
    def summary_line(self) -> str:
        return self.lines[self.summary_index]


def load_nodeset(path: Path) -> NodesetFile:
    text = path.read_text(encoding="utf-8", errors="replace")
    # Keep line endings out of logic; rewrite with \n.
    raw_lines = text.splitlines()
    if not raw_lines:
        raise ValueError(f"{path.name}: empty file")

    summary_index = -1
    inlet: list[int] = []
    wall: list[int] = []
    outlet: list[int] = []
    for i, line in enumerate(raw_lines):
        m = NODESET_LINE_RE.match(line.strip())
        if not m:
            continue
        summary_index = i
        inlet = parse_ints(m.group(2))
        wall = parse_ints(m.group(3))
        outlet = parse_ints(m.group(4))
        break
    if summary_index < 0:
        raise ValueError(f"{path.name}: no 'nodeset: inlet ... wall ... outlet ...' line")
    if not wall:
        raise ValueError(f"{path.name}: wall id list is empty")
    if not inlet:
        raise ValueError(f"{path.name}: inlet id list is empty")
    return NodesetFile(
        path=path,
        lines=raw_lines,
        summary_index=summary_index,
        inlet=inlet,
        wall=wall,
        outlet=outlet,
    )


def format_nodeset_summary(inlet: list[int], wall: list[int], outlet: list[int]) -> str:
    inlet_s = " ".join(str(x) for x in inlet)
    wall_s = " ".join(str(x) for x in wall)
    outlet_s = " ".join(str(x) for x in outlet)
    if outlet_s:
        return f"nodeset: inlet {inlet_s}  wall {wall_s}  outlet {outlet_s}"
    return f"nodeset: inlet {inlet_s}  wall {wall_s}  outlet"


def read_exo_ids(exo_path: Path) -> tuple[list[int], list[int]]:
    ds = Dataset(str(exo_path), "r")
    try:
        if "ss_prop1" not in ds.variables or "ns_prop1" not in ds.variables:
            raise ValueError(f"{exo_path.name}: missing ss_prop1 or ns_prop1")
        ss = [int(x) for x in ds.variables["ss_prop1"][:].tolist()]
        ns = [int(x) for x in ds.variables["ns_prop1"][:].tolist()]
    finally:
        ds.close()
    if len(ss) != len(ns):
        raise ValueError(
            f"{exo_path.name}: ss_prop1 length {len(ss)} != ns_prop1 length {len(ns)}"
        )
    if len(ss) != len(set(ss)):
        raise ValueError(f"{exo_path.name}: duplicate ss_prop1 ids")
    if len(ns) != len(set(ns)):
        raise ValueError(f"{exo_path.name}: duplicate ns_prop1 ids")
    return ss, ns


def write_exo_ids(exo_path: Path, ss_ids: list[int], ns_ids: list[int]) -> None:
    ds = Dataset(str(exo_path), "r+")
    try:
        ds.variables["ss_prop1"][:] = ss_ids
        ds.variables["ns_prop1"][:] = ns_ids
    finally:
        ds.close()


def build_id_maps(
    area_rank_ss: list[int], ss_ids: list[int], ns_ids: list[int]
) -> tuple[dict[int, int], dict[int, int]]:
    if set(area_rank_ss) != set(ss_ids):
        missing = sorted(set(ss_ids) - set(area_rank_ss))
        extra = sorted(set(area_rank_ss) - set(ss_ids))
        raise ValueError(
            f"Nodeset side ids != exo ss_prop1 "
            f"(missing_from_nodeset={missing} extra_in_nodeset={extra})"
        )
    if len(area_rank_ss) != len(ss_ids):
        raise ValueError("Nodeset area-rank list length != number of exo side sets")

    ss_index = {sid: i for i, sid in enumerate(ss_ids)}
    rank_ns = [ns_ids[ss_index[sid]] for sid in area_rank_ss]
    pool_ss = sorted(ss_ids)
    pool_ns = sorted(ns_ids)
    ss_map = {old: pool_ss[i] for i, old in enumerate(area_rank_ss)}
    ns_map = {old: pool_ns[i] for i, old in enumerate(rank_ns)}
    return ss_map, ns_map


def remap_lists(values: list[int], mapping: dict[int, int]) -> list[int]:
    return [mapping[v] for v in values]


def assert_post_remap(
    new_wall: list[int],
    new_inlet: list[int],
    new_outlet: list[int],
    new_ss: list[int],
    old_ss: list[int],
    old_ns: list[int],
    new_ns: list[int],
) -> None:
    if set(new_ss) != set(old_ss):
        raise AssertionError("side ID set changed after remap")
    if set(new_ns) != set(old_ns):
        raise AssertionError("node ID set changed after remap")
    if set(new_wall + new_inlet + new_outlet) != set(new_ss):
        raise AssertionError("remapped Nodeset ids != remapped ss_prop1")
    pool = sorted(new_ss)
    if new_wall != [pool[0]]:
        raise AssertionError(f"wall {new_wall} != min ss {pool[0]}")
    if len(pool) < 2 or new_inlet != [pool[1]]:
        raise AssertionError(f"inlet {new_inlet} != second-smallest ss {pool[1:][:1]}")
    if new_outlet != pool[2:]:
        raise AssertionError(f"outlet {new_outlet} != remaining sorted ids {pool[2:]}")


@dataclass
class CaseResult:
    stem: str
    exo: Path
    nodeset: Path
    ss_map: dict[int, int]
    ns_map: dict[int, int]
    old_ss: list[int]
    new_ss: list[int]
    old_ns: list[int]
    new_ns: list[int]
    old_summary: str
    new_summary: str
    old_inlet: list[int]
    old_wall: list[int]
    old_outlet: list[int]
    new_inlet: list[int]
    new_wall: list[int]
    new_outlet: list[int]
    adapter_rows: list[str]


def find_cases(root: Path, only: str | None) -> list[tuple[Path, Path]]:
    pairs: list[tuple[Path, Path]] = []
    for exo in sorted(root.glob("PV_*.exo")):
        stem = exo.stem  # PV_K2-5
        if only and stem != only and stem.removeprefix("PV_") != only:
            continue
        nodeset = root / f"Nodeset_{stem}"
        pairs.append((exo, nodeset))
    return pairs


def process_case(exo: Path, nodeset_path: Path) -> CaseResult:
    nsfile = load_nodeset(nodeset_path)
    old_ss, old_ns = read_exo_ids(exo)
    ss_map, ns_map = build_id_maps(nsfile.area_rank_ss, old_ss, old_ns)

    new_ss = remap_lists(old_ss, ss_map)
    new_ns = remap_lists(old_ns, ns_map)
    new_wall = remap_lists(nsfile.wall, ss_map)
    new_inlet = remap_lists(nsfile.inlet, ss_map)
    new_outlet = remap_lists(nsfile.outlet, ss_map)
    assert_post_remap(new_wall, new_inlet, new_outlet, new_ss, old_ss, old_ns, new_ns)

    adapter_rows = [
        line
        for i, line in enumerate(nsfile.lines)
        if i != nsfile.summary_index and line.strip() and not line.lower().startswith("id ")
    ]
    new_summary = format_nodeset_summary(new_inlet, new_wall, new_outlet)
    return CaseResult(
        stem=exo.stem,
        exo=exo,
        nodeset=nodeset_path,
        ss_map=ss_map,
        ns_map=ns_map,
        old_ss=old_ss,
        new_ss=new_ss,
        old_ns=old_ns,
        new_ns=new_ns,
        old_summary=nsfile.summary_line,
        new_summary=new_summary,
        old_inlet=list(nsfile.inlet),
        old_wall=list(nsfile.wall),
        old_outlet=list(nsfile.outlet),
        new_inlet=new_inlet,
        new_wall=new_wall,
        new_outlet=new_outlet,
        adapter_rows=adapter_rows,
    )


def backup(path: Path, backup_dir: Path | None = None) -> Path:
    if backup_dir is not None:
        backup_dir.mkdir(parents=True, exist_ok=True)
        dest = backup_dir / path.name
        shutil.copy2(path, dest)
        return dest
    bak = path.with_name(path.name + ".bak")
    if not bak.exists():
        shutil.copy2(path, bak)
    return bak


def write_nodeset_summary(src: Path, dst: Path, new_summary: str) -> None:
    text = src.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    nsfile = load_nodeset(src)
    lines[nsfile.summary_index] = new_summary
    newline = "\r\n" if "\r\n" in text else "\n"
    body = newline.join(lines)
    if text.endswith("\n"):
        body += newline
    dst.write_bytes(body.encode("utf-8"))


def write_case(
    result: CaseResult,
    *,
    make_backup: bool,
    out_dir: Path | None,
    backup_dir: Path | None,
) -> None:
    exo_dst = result.exo
    ns_dst = result.nodeset
    if out_dir is not None:
        out_dir.mkdir(parents=True, exist_ok=True)
        exo_dst = out_dir / result.exo.name
        ns_dst = out_dir / result.nodeset.name
        shutil.copy2(result.exo, exo_dst)
        write_nodeset_summary(result.nodeset, ns_dst, result.new_summary)
        write_exo_ids(exo_dst, result.new_ss, result.new_ns)
        return

    if make_backup:
        backup(result.exo, backup_dir)
        backup(result.nodeset, backup_dir)
    write_exo_ids(exo_dst, result.new_ss, result.new_ns)
    write_nodeset_summary(result.nodeset, ns_dst, result.new_summary)


def print_case(result: CaseResult) -> None:
    print(f"== {result.stem} ==")
    print(f"  {result.old_summary}")
    print(f"  {result.new_summary}")
    print(f"  wall {result.old_wall} -> {result.new_wall}")
    print(f"  inlet {result.old_inlet} -> {result.new_inlet}")
    changed_ss = sum(1 for a, b in zip(result.old_ss, result.new_ss) if a != b)
    changed_ns = sum(1 for a, b in zip(result.old_ns, result.new_ns) if a != b)
    print(
        f"  ss_prop1 changed {changed_ss}/{len(result.old_ss)}  "
        f"ns_prop1 changed {changed_ns}/{len(result.old_ns)}"
    )
    print(f"  adapter data rows: {len(result.adapter_rows)} (unchanged)")


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--dir",
        type=Path,
        default=Path(r"C:\Users\18490\Desktop\out\K2-1-28"),
        help="Directory containing PV_*.exo and Nodeset_PV_*",
    )
    p.add_argument(
        "--case",
        default=None,
        help="Only this stem, e.g. PV_K2-5 or K2-5",
    )
    p.add_argument(
        "--apply",
        action="store_true",
        help="Write changes (default is dry-run)",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print mapping only (default if --apply is not set)",
    )
    p.add_argument(
        "--no-backup",
        action="store_true",
        help="When applying in place, do not write backup copies",
    )
    p.add_argument(
        "--backup-dir",
        type=Path,
        default=None,
        help="Directory for pre-remap copies (original names). Default: <dir>/original_before_entity_id_remap",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Write remapped copies here instead of modifying the source files",
    )
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.dir
    if not root.is_dir():
        print(f"Not a directory: {root}", file=sys.stderr)
        return 2

    apply = bool(args.apply) and not args.dry_run
    pairs = find_cases(root, args.case)
    if not pairs:
        print(f"No PV_*.exo cases found in {root}", file=sys.stderr)
        return 1

    errors = 0
    results: list[CaseResult] = []
    for exo, nodeset in pairs:
        if not nodeset.is_file():
            print(f"SKIP {exo.stem}: missing {nodeset.name}", file=sys.stderr)
            errors += 1
            continue
        try:
            result = process_case(exo, nodeset)
        except Exception as exc:  # noqa: BLE001 — report per-case and continue
            print(f"FAIL {exo.stem}: {exc}", file=sys.stderr)
            errors += 1
            continue
        print_case(result)
        results.append(result)

    if apply:
        out_dir = args.out_dir
        make_backup = not args.no_backup and out_dir is None
        backup_dir = None
        if make_backup:
            backup_dir = args.backup_dir
            if backup_dir is None:
                backup_dir = root / "original_before_entity_id_remap"
            backup_dir = backup_dir.resolve()
            print(f"Backups -> {backup_dir}")
        for result in results:
            write_case(
                result,
                make_backup=make_backup,
                out_dir=out_dir,
                backup_dir=backup_dir,
            )
            print(f"WROTE {result.stem}")
        print(f"Applied {len(results)} case(s).")
    else:
        print(f"Dry-run {len(results)} case(s). Pass --apply to write.")

    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
