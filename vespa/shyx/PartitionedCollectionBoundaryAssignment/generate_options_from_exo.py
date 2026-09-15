#!/usr/bin/env python3
"""Batch-generate solver options files from Exodus meshes via Boundary Assignment.

Run with ParaView's pvpython (plugin must include Repart):

  pvpython generate_options_from_exo.py

Defaults: HV + HV-MESH folders from the 2026-09 WeChat drop, VESPAPlugin from this
repo's build tree. Filename chooses the template:
  HV / aorta  -> Single outlet (HV template)
  PV / plaque -> Single inlet (PV template)

Filter settings: upstream defaults plus Repart on. Only options files are written
(no exo / Nodeset / pvsm), named options_<stem> next to each mesh.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import traceback
from pathlib import Path

DEFAULT_INPUTS = [
    Path(r"c:\Users\18490\Documents\xwechat_files\wxid_wvlzg4yx6dw222_72d1\msg\file\2026-09\HV\HV"),
    Path(r"c:\Users\18490\Documents\xwechat_files\wxid_wvlzg4yx6dw222_72d1\msg\file\2026-09\HV\HV-MESH"),
]

HV_TOKEN = re.compile(r"(^|[_\-])hv([_\-.]|$)", re.IGNORECASE)
PV_TOKEN = re.compile(r"(^|[_\-])pv([_\-.]|$)", re.IGNORECASE)
FLAG_LINE = re.compile(r"^([ \t]*)(-f|-nodeset_file|-sideset_file|-output)([ \t]+)(\S+)(.*)$")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def default_plugin() -> Path:
    return (
        repo_root()
        / "build"
        / "bin"
        / "paraview-6.0"
        / "plugins"
        / "VESPAPlugin"
        / "Release"
        / "VESPAPlugin.dll"
    )


def infer_mode(path: Path) -> int:
    """0 = Single inlet (PV), 1 = Single outlet (HV)."""
    name = path.name.lower()
    plaque = "plaque" in name
    aorta = "aorta" in name
    if plaque and not aorta:
        return 0
    if aorta and not plaque:
        return 1
    hv = bool(HV_TOKEN.search(name))
    pv = bool(PV_TOKEN.search(name))
    if hv and not pv:
        return 1
    if pv and not hv:
        return 0
    raise ValueError(f"cannot infer HV/PV from filename: {path.name}")


def mode_label(mode: int) -> str:
    return "HV" if mode == 1 else "PV"


def collect_exo_files(inputs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for raw in inputs:
        root = raw.expanduser().resolve()
        if root.is_file() and root.suffix.lower() == ".exo":
            files.append(root)
            continue
        if not root.is_dir():
            raise FileNotFoundError(f"input path does not exist: {root}")
        files.extend(sorted(p for p in root.glob("*.exo") if p.is_file()))
    # Keep duplicates (same stem in HV vs HV-MESH) — each writes beside its mesh.
    return files


def replace_flag_leaf(text: str, flag: str, new_leaf: str) -> str:
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    replaced = False
    for line in lines:
        m = FLAG_LINE.match(line.rstrip("\r\n"))
        if not replaced and m and m.group(2) == flag:
            indent, name, sep, path, rest = m.groups()
            parts = path.replace("\\", "/").split("/")
            parts[-1] = new_leaf
            newline = "\n" if line.endswith("\n") else ""
            out.append(f"{indent}{name}{sep}{'/'.join(parts)}{rest}{newline}")
            replaced = True
        else:
            out.append(line)
    return "".join(out)


def rewrite_template_paths(text: str, exo_path: Path) -> str:
    stem = exo_path.stem
    text = replace_flag_leaf(text, "-f", exo_path.name)
    text = replace_flag_leaf(text, "-nodeset_file", f"Nodeset_{stem}")
    text = replace_flag_leaf(text, "-sideset_file", f"Sideset_{stem}")
    text = replace_flag_leaf(text, "-output", f"Results_{stem}_Solution")
    return text


def count_inlet_values(text: str) -> int:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("-inlet_nx"):
            payload = stripped.split(None, 1)
            if len(payload) < 2:
                return 0
            return len([p for p in payload[1].split(",") if p.strip()])
    return 0


def enable_ioss_entities(reader) -> None:
    reader.UpdatePipelineInformation()
    for name in (
        "ElementBlocks",
        "SideSets",
        "NodeSets",
        "FaceBlocks",
        "EdgeBlocks",
        "ElementSets",
        "FaceSets",
        "EdgeSets",
        "StructuredBlocks",
    ):
        if not hasattr(reader, name):
            continue
        prop = getattr(reader, name)
        available = getattr(prop, "Available", None)
        if available:
            setattr(reader, name, list(available))


def read_opt_text(filt) -> str:
    from paraview import servermanager

    data = servermanager.Fetch(filt)
    if data is None:
        raise RuntimeError("Fetch() returned None")
    arr = data.GetFieldData().GetAbstractArray("SHYXInletOptText")
    if arr is None or arr.GetNumberOfValues() < 1:
        raise RuntimeError("output FieldData missing SHYXInletOptText")
    return str(arr.GetValue(0))


def process_one(pv, exo: Path, out_path: Path, mode: int, rewrite_paths: bool) -> tuple[int, Path]:
    reader = pv.IOSSReader(FileName=str(exo))
    try:
        enable_ioss_entities(reader)
        reader.UpdatePipeline()

        filt = pv.SHYXPartitionedCollectionBoundaryAssignment(Input=reader)
        try:
            filt.Repart = 1
            filt.FlowBoundaryMode = mode
            filt.UpdatePipeline()
            text = read_opt_text(filt)
        finally:
            pv.Delete(filt)
    finally:
        pv.Delete(reader)

    if rewrite_paths:
        text = rewrite_template_paths(text, exo)
    if not text.endswith("\n"):
        text += "\n"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8", newline="\n")
    return count_inlet_values(text), out_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "inputs",
        nargs="*",
        type=Path,
        default=DEFAULT_INPUTS,
        help="Exodus files or directories of *.exo (default: the two HV batches)",
    )
    parser.add_argument(
        "--plugin",
        type=Path,
        default=default_plugin(),
        help="VESPAPlugin.dll (must include Repart)",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Write all options_STEM files here instead of beside each mesh",
    )
    parser.add_argument(
        "--no-rewrite-paths",
        action="store_true",
        help="Keep template -f / nodeset / sideset / output paths unchanged",
    )
    parser.add_argument("--limit", type=int, default=0, help="Process at most N files (0 = all)")
    parser.add_argument("--dry-run", action="store_true", help="List files and inferred mode only")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    files = collect_exo_files(list(args.inputs))
    if args.limit > 0:
        files = files[: args.limit]
    if not files:
        print("No .exo files found.", file=sys.stderr)
        return 1

    planned: list[tuple[Path, int, Path]] = []
    for exo in files:
        mode = infer_mode(exo)
        name = f"options_{exo.stem}"
        dest_dir = args.out_dir.expanduser().resolve() if args.out_dir else exo.parent
        planned.append((exo, mode, dest_dir / name))

    print(f"{len(planned)} mesh(es)")
    for exo, mode, out_path in planned:
        print(f"  {mode_label(mode):2}  {exo.name}  ->  {out_path}")

    if args.dry_run:
        return 0

    plugin = args.plugin.expanduser().resolve()
    if not plugin.is_file():
        print(f"Plugin not found: {plugin}", file=sys.stderr)
        return 1

    # Load after listing so --dry-run does not need ParaView.
    from paraview import simple as pv

    pv.LoadPlugin(str(plugin), remote=False, ns=pv.__dict__)
    if not hasattr(pv, "SHYXPartitionedCollectionBoundaryAssignment"):
        print("Plugin loaded but SHYXPartitionedCollectionBoundaryAssignment is missing.", file=sys.stderr)
        return 1

    ok = 0
    failed: list[tuple[Path, str]] = []
    rewrite = not args.no_rewrite_paths
    for i, (exo, mode, out_path) in enumerate(planned, start=1):
        print(f"[{i}/{len(planned)}] {mode_label(mode)} {exo.name} ...", flush=True)
        try:
            n_inlets, written = process_one(pv, exo, out_path, mode, rewrite)
            print(f"    wrote {written.name}  inlets={n_inlets}", flush=True)
            ok += 1
        except Exception as exc:
            failed.append((exo, f"{exc}"))
            print(f"    FAILED: {exc}", flush=True)
            traceback.print_exc()

    print(f"done: {ok} ok, {len(failed)} failed")
    for exo, msg in failed:
        print(f"  FAIL {exo}: {msg}", file=sys.stderr)
    return 0 if not failed else 2


if __name__ == "__main__":
    # Avoid OpenFOAM/snappy plugin spam drowning the progress lines.
    os.environ.setdefault("KMP_WARNINGS", "0")
    sys.exit(main())
