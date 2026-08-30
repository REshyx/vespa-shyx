#!/usr/bin/env python3
"""Reorder Exodus side/node *storage* so slot order matches ENTITY_ID.

After remap_exported_pv_entity_ids.py, ss_prop1/ns_prop1 are already rank-numbered
but the arrays still sit in the original creation order (wall often last). This
rewrites a new .exo where:

  ss1/ns1 = smallest ID (wall after PV remap)
  ss2/ns2 = next ID (inlet)
  ...

Geometry, IDs, and names move together. Nodeset text does not change: it already
lists real IDs, not storage indices.

NETCDF3 dimensions cannot be resized in place, so output is always a new file.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

import numpy as np
from netCDF4 import Dataset

DIM_NS = re.compile(r"^num_nod_ns(\d+)$")
DIM_SS = re.compile(r"^num_side_ss(\d+)$")
VAR_NS = re.compile(r"^(node_ns|dist_fact_ns)(\d+)$")
VAR_SS = re.compile(r"^(elem_ss|side_ss)(\d+)$")
VAR_SSET = re.compile(r"^vals_sset_var(\d+)ss(\d+)$")
PERMUTE_AXIS0 = (
    "ns_status",
    "ns_prop1",
    "ns_names",
    "ss_status",
    "ss_prop1",
    "ss_names",
    "sset_var_tab",
)


def permutation_by_ids(ids: list[int]) -> list[int]:
    return sorted(range(len(ids)), key=lambda i: (ids[i], i))


def already_sorted(ids: list[int]) -> bool:
    return ids == sorted(ids)


def copy_ncattrs(src, dst, skip: set[str] | None = None) -> None:
    skip = skip or set()
    for name in src.ncattrs():
        if name in skip:
            continue
        dst.setncattr(name, src.getncattr(name))


def create_var(dst: Dataset, src_var, name: str, dimensions: tuple[str, ...]):
    fill = None
    attrs = list(src_var.ncattrs())
    if "_FillValue" in attrs:
        fill = src_var.getncattr("_FillValue")
        attrs = [a for a in attrs if a != "_FillValue"]
    kwargs: dict = {"fill_value": fill} if fill is not None else {}
    out = dst.createVariable(name, src_var.dtype, dimensions, **kwargs)
    for a in attrs:
        out.setncattr(a, src_var.getncattr(a))
    return out


def reorder_exo(src_path: Path, dst_path: Path) -> dict:
    src = Dataset(str(src_path), "r")
    try:
        if "ss_prop1" not in src.variables or "ns_prop1" not in src.variables:
            raise ValueError(f"{src_path.name}: missing ss_prop1 or ns_prop1")
        ss_ids = [int(x) for x in src.variables["ss_prop1"][:].tolist()]
        ns_ids = [int(x) for x in src.variables["ns_prop1"][:].tolist()]
        if len(ss_ids) != len(ns_ids):
            raise ValueError(
                f"{src_path.name}: {len(ss_ids)} side sets vs {len(ns_ids)} node sets"
            )
        order_ss = permutation_by_ids(ss_ids)
        order_ns = permutation_by_ids(ns_ids)
        if order_ss != order_ns:
            raise ValueError(
                f"{src_path.name}: side/node ID permutations differ; "
                f"ss={ss_ids} ns={ns_ids}"
            )
        order = order_ss
        new_ss = [ss_ids[i] for i in order]
        new_ns = [ns_ids[i] for i in order]
        ss_sizes = [
            len(src.dimensions[f"num_side_ss{i + 1}"]) for i in range(len(ss_ids))
        ]
        ns_sizes = [
            len(src.dimensions[f"num_nod_ns{i + 1}"]) for i in range(len(ns_ids))
        ]
        info = {
            "src": src_path,
            "dst": dst_path,
            "n": len(order),
            "already_sorted": already_sorted(ss_ids),
            "old_ss": ss_ids,
            "new_ss": new_ss,
            "old_ns": ns_ids,
            "new_ns": new_ns,
            "old_ss_sizes": ss_sizes,
            "new_ss_sizes": [ss_sizes[i] for i in order],
            "old_ns_sizes": ns_sizes,
            "new_ns_sizes": [ns_sizes[i] for i in order],
            "order": [i + 1 for i in order],
        }

        dst_path.parent.mkdir(parents=True, exist_ok=True)
        in_place = src_path.resolve() == dst_path.resolve()
        write_path = dst_path.with_name(dst_path.name + ".tmp") if in_place else dst_path
        if write_path.exists():
            write_path.unlink()

        dst = Dataset(str(write_path), "w", format=src.data_model)
        try:
            copy_ncattrs(src, dst)
            for dim_name, dim in src.dimensions.items():
                m_ns = DIM_NS.match(dim_name)
                m_ss = DIM_SS.match(dim_name)
                if m_ns:
                    new_i = int(m_ns.group(1))
                    size = ns_sizes[order[new_i - 1]]
                    dst.createDimension(dim_name, size)
                elif m_ss:
                    new_i = int(m_ss.group(1))
                    size = ss_sizes[order[new_i - 1]]
                    dst.createDimension(dim_name, size)
                else:
                    dst.createDimension(
                        dim_name, None if dim.isunlimited() else len(dim)
                    )

            order_arr = np.asarray(order, dtype=np.intp)
            for var_name, src_var in src.variables.items():
                out = create_var(dst, src_var, var_name, src_var.dimensions)
                m_ns = VAR_NS.match(var_name)
                m_ss = VAR_SS.match(var_name)
                m_sset = VAR_SSET.match(var_name)
                if m_ns:
                    new_i = int(m_ns.group(2))
                    old_i = order[new_i - 1] + 1
                    old_name = f"{m_ns.group(1)}{old_i}"
                    out[:] = src.variables[old_name][:]
                elif m_ss:
                    new_i = int(m_ss.group(2))
                    old_i = order[new_i - 1] + 1
                    old_name = f"{m_ss.group(1)}{old_i}"
                    out[:] = src.variables[old_name][:]
                elif m_sset:
                    new_i = int(m_sset.group(2))
                    old_i = order[new_i - 1] + 1
                    old_name = f"vals_sset_var{m_sset.group(1)}ss{old_i}"
                    out[:] = src.variables[old_name][:]
                elif var_name in PERMUTE_AXIS0:
                    data = src_var[:]
                    out[:] = np.take(data, order_arr, axis=0)
                else:
                    out[:] = src_var[:]
        finally:
            dst.close()
    finally:
        src.close()

    if in_place:
        if dst_path.exists():
            dst_path.unlink()
        write_path.replace(dst_path)
    return info


def print_info(info: dict) -> None:
    print(f"== {info['src'].name} -> {info['dst']} ==")
    print(f"  already sorted: {info['already_sorted']}")
    print(f"  old ss_prop1: {info['old_ss']}")
    print(f"  new ss_prop1: {info['new_ss']}")
    print(f"  old ns_prop1: {info['old_ns']}")
    print(f"  new ns_prop1: {info['new_ns']}")
    print(f"  slot map new<-old: {info['order']}")
    print(f"  old ss sizes: {info['old_ss_sizes']}")
    print(f"  new ss sizes: {info['new_ss_sizes']}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--dir",
        type=Path,
        default=Path(r"C:\Users\18490\Desktop\out\K2-1-28"),
        help="Directory containing remapped PV_*.exo files",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Write rewritten exo here. Omit with --apply to overwrite sources.",
    )
    p.add_argument(
        "--apply",
        action="store_true",
        help="Overwrite source PV_*.exo after copying them to --backup-dir",
    )
    p.add_argument(
        "--backup-dir",
        type=Path,
        default=None,
        help="Directory for pre-reorder copies. Default: <dir>/original_before_storage_reorder",
    )
    p.add_argument(
        "--no-backup",
        action="store_true",
        help="Do not copy sources before overwriting",
    )
    p.add_argument(
        "--files",
        nargs="+",
        default=None,
        help="Basenames or stems to process, e.g. PV_K2-5.exo PV_K2-26.exo",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print permutation only",
    )
    return p.parse_args(argv)


def resolve_files(root: Path, names: list[str] | None) -> list[Path]:
    if not names:
        return sorted(root.glob("PV_*.exo"))
    out: list[Path] = []
    for name in names:
        p = Path(name)
        if not p.is_absolute():
            p = root / p
        if p.suffix.lower() != ".exo":
            p = p.with_suffix(".exo") if p.suffix else Path(str(p) + ".exo")
            if not p.is_file():
                p = root / p.name
        if not p.is_file():
            alt = root / f"{Path(name).stem}.exo"
            p = alt
        if not p.is_file():
            raise FileNotFoundError(name)
        out.append(p)
    return out


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.dir
    if not root.is_dir():
        print(f"Not a directory: {root}", file=sys.stderr)
        return 2
    try:
        files = resolve_files(root, args.files)
    except FileNotFoundError as exc:
        print(f"Missing file: {exc}", file=sys.stderr)
        return 1
    if not files:
        print(f"No PV_*.exo in {root}", file=sys.stderr)
        return 1
    apply = bool(args.apply) and not args.dry_run
    if not args.dry_run and args.out_dir is None and not apply:
        print("Pass --apply (in-place) or --out-dir, or --dry-run", file=sys.stderr)
        return 2
    out_dir = args.out_dir
    backup_dir = None
    if apply and not args.no_backup and out_dir is None:
        backup_dir = args.backup_dir or (root / "original_before_storage_reorder")
        backup_dir = backup_dir.resolve()
        backup_dir.mkdir(parents=True, exist_ok=True)
        print(f"Backups -> {backup_dir}")
    errors = 0
    for src in files:
        dst = (out_dir / src.name) if out_dir is not None else src
        if args.dry_run:
            src_ds = Dataset(str(src), "r")
            try:
                ss_ids = [int(x) for x in src_ds.variables["ss_prop1"][:].tolist()]
                ns_ids = [int(x) for x in src_ds.variables["ns_prop1"][:].tolist()]
                order = permutation_by_ids(ss_ids)
                info = {
                    "src": src,
                    "dst": dst,
                    "already_sorted": already_sorted(ss_ids),
                    "old_ss": ss_ids,
                    "new_ss": [ss_ids[i] for i in order],
                    "old_ns": ns_ids,
                    "new_ns": [ns_ids[i] for i in order],
                    "old_ss_sizes": [
                        len(src_ds.dimensions[f"num_side_ss{i + 1}"])
                        for i in range(len(ss_ids))
                    ],
                    "new_ss_sizes": [
                        len(src_ds.dimensions[f"num_side_ss{order[i] + 1}"])
                        for i in range(len(ss_ids))
                    ],
                    "order": [i + 1 for i in order],
                }
            finally:
                src_ds.close()
            print_info(info)
            continue
        if backup_dir is not None:
            shutil.copy2(src, backup_dir / src.name)
        try:
            info = reorder_exo(src, dst)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {src.name}: {exc}", file=sys.stderr)
            errors += 1
            continue
        print_info(info)
        print(f"WROTE {dst}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
