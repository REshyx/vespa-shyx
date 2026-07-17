"""Rename Exodus node-set names with a prefix so they no longer collide with side-set names.

vtkIOSSReader caches nodeblock field subsets by entity name; identical ns/ss names
(e.g. Wall/Wall) can mis-assign point 'ids' on side sets when RemoveUnusedPoints is on.
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

from netCDF4 import Dataset
import numpy as np

PREFIX = "node_"
SRC = Path(
    r"C:\Users\18490\Desktop\HiBFlowFluid-v1.0\HiBFlowFluid-v1.0\Testes\meshfiles\Tube_ref_fluid1.exo"
)
DST = Path(r"C:\Users\18490\Desktop\Tube_ref_fluid1_ns_prefixed.exo")


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


def main() -> int:
    if not SRC.is_file():
        print(f"Source not found: {SRC}", file=sys.stderr)
        return 1

    shutil.copy2(SRC, DST)
    ds = Dataset(str(DST), "r+")
    if "ns_names" not in ds.variables:
        print("No ns_names in file; nothing to do.", file=sys.stderr)
        ds.close()
        return 1

    var = ds.variables["ns_names"]
    old = decode_names(var[:])
    width = var.shape[1]
    new = []
    for n in old:
        nn = n if n.startswith(PREFIX) else PREFIX + n
        new.append(nn)

    for i, nn in enumerate(new):
        var[i, :] = encode_name(nn, width)

    ss = decode_names(ds.variables["ss_names"][:]) if "ss_names" in ds.variables else []
    ds.close()

    print(f"Wrote: {DST}")
    print(f"ns_names: {old} -> {new}")
    print(f"ss_names (unchanged): {ss}")
    print(f"overlap after: {sorted(set(new) & set(ss))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
