"""Copy OpenFOAM headers into a flat lnInclude directory.

On Windows the destination is NTFS case-sensitive so lduMatrix.H and
LduMatrix.H can coexist, and so #include <string> does not pick up string.H.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys


def ntfs_case_sensitive(path: str) -> None:
    if os.name != "nt":
        return
    os.makedirs(path, exist_ok=True)
    subprocess.run(
        ["fsutil.exe", "file", "setCaseSensitiveInfo", os.path.abspath(path), "enable"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


src, dst = sys.argv[1], sys.argv[2]
ntfs_case_sensitive(dst)
exts = {".H", ".h", ".hxx", ".hpp", ".I", ".txx", ".C", ".c"}
for root, _, files in os.walk(src):
    for name in files:
        if os.path.splitext(name)[1] in exts:
            dest = os.path.join(dst, name)
            if not os.path.exists(dest):
                shutil.copy2(os.path.join(root, name), dest)
