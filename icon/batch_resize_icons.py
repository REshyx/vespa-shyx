#!/usr/bin/env python3
"""
Batch resize PNG icons (keep aspect ratio, limit longest edge).

  python batch_resize_icons.py [--max 128] [--dir .] [--dry-run]

Requires: pip install pillow
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    p = argparse.ArgumentParser(description="Batch resize PNG icons.")
    p.add_argument(
        "--max",
        type=int,
        default=128,
        metavar="PX",
        help="Maximum width/height in pixels (default: 128)",
    )
    p.add_argument(
        "--dir",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Directory containing PNG files (default: this script's folder)",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions only, do not write files",
    )
    args = p.parse_args()
    root: Path = args.dir.resolve()
    if not root.is_dir():
        raise SystemExit(f"Not a directory: {root}")

    for path in sorted(root.glob("*.png")):
        im = Image.open(path)
        if im.mode in ("RGBA", "LA") or (im.mode == "P" and "transparency" in im.info):
            im = im.convert("RGBA")
        elif im.mode != "RGB":
            im = im.convert("RGB")
        w, h = im.size
        m = max(w, h)
        if m <= args.max:
            print(f"skip (already <= {args.max}px): {path.name} {w}x{h}")
            continue
        scale = args.max / m
        nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
        out = im.resize((nw, nh), Image.Resampling.LANCZOS)
        print(f"resize: {path.name} {w}x{h} -> {nw}x{nh}")
        if not args.dry_run:
            out.save(path, format="PNG", optimize=True, compress_level=9)


if __name__ == "__main__":
    main()
