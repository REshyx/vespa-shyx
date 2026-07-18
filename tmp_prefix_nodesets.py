"""Rename Exodus node-set names with a prefix so they no longer collide with side-set names."""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

from netCDF4 import Dataset
import numpy as np

PREFIX = "node_"
MESH_DIR = Path(r"C:\Users\18490\Desktop\HiBFlowFluid-v1.0\Testes\meshfiles")
OUT_DIR = Path(r"C:\Users\18490\Desktop")
FILES = [
    "Tube_ref_fluid2.exo",
    "Tube_ref_fluid3.exo",
]


def decode_names(arr) -> list[str]:
    a = np.asarray(arr)
    names: list[str] = []
    for row in a:
        parts = []
        for x in row:
            b = x if isinstance(x, (bytes, np.bytes_)) else bytes([int(x)])
            if not b or b == b"\x00":
                break
            parts.append(bytes(b))
        names.append(b"".join(parts).decode("ascii", "replace").strip())
    return names


def encode_name(name: str, width: int) -> np.ndarray:
    raw = name.encode("ascii", "replace")[: width - 1]
    out = np.full(width, b"\x00", dtype="S1")
    for i, b in enumerate(raw):
        out[i] = bytes([b])
    return out


def process(src: Path, dst: Path) -> None:
    if not src.is_file():
        raise FileNotFoundError(src)
    shutil.copy2(src, dst)
    ds = Dataset(str(dst), "r+")
    if "ns_names" not in ds.variables:
        ds.close()
        raise RuntimeError(f"No ns_names in {src}")

    var = ds.variables["ns_names"]
    old = decode_names(var[:])
    width = var.shape[1]
    new = [n if n.startswith(PREFIX) else PREFIX + n for n in old]
    for i, nn in enumerate(new):
        var[i, :] = encode_name(nn, width)

    ss = decode_names(ds.variables["ss_names"][:]) if "ss_names" in ds.variables else []
    ds.close()

    print(f"Wrote: {dst}")
    print(f"  ns_names: {old} -> {new}")
    print(f"  ss_names (unchanged): {ss}")
    print(f"  overlap after: {sorted(set(new) & set(ss))}")


def main() -> int:
    for name in FILES:
        src = MESH_DIR / name
        dst = OUT_DIR / f"{Path(name).stem}_ns_prefixed.exo"
        process(src, dst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
